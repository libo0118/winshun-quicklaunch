#!/usr/bin/env python3
"""Download, verify SHA-256, extract Qt 6.12.0 MSVC2022 64-bit SDK archives."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path, PureWindowsPath

try:
    import py7zr
except ImportError:  # pragma: no cover - installed in CI before running
    py7zr = None  # type: ignore[assignment]

CHUNK_SIZE = 1024 * 1024
USER_AGENT = "local.quicklaunch-ci/1.0"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(CHUNK_SIZE)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest().lower()


def download_and_verify(entry: dict[str, str], cache_dir: Path) -> Path:
    module = entry["module"]
    url = entry["url"]
    expected = entry["sha256"].strip().lower()
    filename = Path(urllib.parse.urlparse(url).path).name
    if not filename:
        raise ValueError(f"Cannot determine archive filename from URL: {url}")

    target = cache_dir / filename
    if target.is_file():
        actual = sha256_file(target)
        if actual == expected:
            print(f"[qt-sdk] Cache hit ({module}): {target}")
            return target
        print(f"[qt-sdk] Cache SHA-256 mismatch for {module}; re-downloading", file=sys.stderr)
        target.unlink(missing_ok=True)

    part_path = cache_dir / f"{filename}.part"
    last_err: Exception | None = None
    for attempt in range(1, 4):
        try:
            print(f"[qt-sdk] Downloading {module} (attempt {attempt}/3): {url}")
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            digest = hashlib.sha256()
            with urllib.request.urlopen(req, timeout=120) as resp, part_path.open("wb") as out:
                while True:
                    chunk = resp.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    out.write(chunk)
                    digest.update(chunk)
            actual = digest.hexdigest().lower()
            if actual != expected:
                part_path.unlink(missing_ok=True)
                raise RuntimeError(
                    f"SHA-256 mismatch for {module}: expected {expected}, got {actual}"
                )
            os.replace(part_path, target)
            print(f"[qt-sdk] Verified {module}: {actual}")
            return target
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_err = exc
            part_path.unlink(missing_ok=True)
            if attempt < 3:
                time.sleep(2 * attempt)
    raise RuntimeError(f"Failed to download {module} after 3 attempts: {last_err}")


def validate_archive_members(names: list[str], out_root: Path) -> None:
    resolved_root = out_root.resolve()
    for raw_name in names:
        if not raw_name or "\x00" in raw_name:
            raise RuntimeError(f"Unsafe archive member name: {raw_name!r}")
        if PureWindowsPath(raw_name).drive or raw_name.startswith(("/", "\\")):
            raise RuntimeError(f"Absolute path inside archive is forbidden: {raw_name!r}")
        normalized = raw_name.replace("\\", "/")
        parts = [p for p in normalized.split("/") if p]
        if ".." in parts or (parts and ":" in parts[0]):
            raise RuntimeError(f"Path traversal detected in archive member: {raw_name!r}")
        candidate = (resolved_root / normalized).resolve()
        if candidate != resolved_root and not candidate.is_relative_to(resolved_root):
            raise RuntimeError(f"Archive member escapes output directory: {raw_name!r}")


def extract_archive(archive_path: Path, out_dir: Path) -> None:
    if py7zr is None:
        raise RuntimeError("py7zr is required to extract Qt SDK archives (pip install py7zr==1.1.4)")
    print(f"[qt-sdk] Extracting {archive_path.name} -> {out_dir}")
    with py7zr.SevenZipFile(archive_path, mode="r") as archive:
        validate_archive_members(archive.getnames(), out_dir)
        archive.extractall(path=out_dir)


def find_qt_prefix(out_dir: Path) -> Path:
    candidates: list[Path] = []
    for cfg in out_dir.rglob("Qt6Config.cmake"):
        p_qt6 = cfg.parent
        p_cmake = p_qt6.parent
        p_lib = p_cmake.parent
        if (
            p_qt6.name.lower() == "qt6"
            and p_cmake.name.lower() == "cmake"
            and p_lib.name.lower() == "lib"
        ):
            prefix = p_lib.parent.resolve()
            if (prefix / "bin").is_dir():
                candidates.append(prefix)
    if not candidates:
        raise RuntimeError(f"Could not find lib/cmake/Qt6/Qt6Config.cmake under {out_dir}")
    candidates.sort(key=lambda p: len(p.parts))
    return candidates[0]


def write_qt_conf(prefix: Path) -> Path:
    bin_dir = prefix / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    qt_conf = bin_dir / "qt.conf"
    qt_conf.write_text("[Paths]\nPrefix=..\n", encoding="utf-8", newline="\n")
    return qt_conf


def main() -> int:
    default_manifest = Path(__file__).resolve().parent / "qt-sdk.json"
    parser = argparse.ArgumentParser(description="Install pinned Qt 6.12.0 SDK for CI")
    parser.add_argument("--manifest", default=str(default_manifest), help="Path to qt-sdk.json")
    parser.add_argument("--output", required=True, help="Extraction root directory")
    parser.add_argument("--cache", required=True, help="Download cache directory")
    parser.add_argument(
        "--github-env",
        nargs="?",
        const="",
        default=None,
        help="Write QT_CMAKE_PREFIX to GITHUB_ENV (or given env file path)",
    )
    args = parser.parse_args()

    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    archives = manifest["archives"]
    out_dir = Path(args.output).resolve()
    cache_dir = Path(args.cache).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(archives))) as pool:
        futures = [pool.submit(download_and_verify, entry, cache_dir) for entry in archives]
        downloaded = [fut.result() for fut in futures]

    for archive_path in downloaded:
        extract_archive(archive_path, out_dir)

    prefix = find_qt_prefix(out_dir)
    qt_conf = write_qt_conf(prefix)
    print(f"[qt-sdk] Wrote {qt_conf}")
    print(f"QT_CMAKE_PREFIX={prefix}")

    if args.github_env is not None:
        env_target = args.github_env or os.environ.get("GITHUB_ENV", "")
        if not env_target:
            raise RuntimeError("--github-env set but GITHUB_ENV is empty")
        with Path(env_target).open("a", encoding="utf-8", newline="\n") as fh:
            fh.write(f"QT_CMAKE_PREFIX={prefix}\n")
        print(f"[qt-sdk] Exported QT_CMAKE_PREFIX to {env_target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
