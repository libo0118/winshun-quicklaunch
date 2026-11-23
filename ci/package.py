#!/usr/bin/env python3
"""Package QuickLaunch.exe and metadata into a clean WinShun .wsplugin archive."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_manifest import validate_manifest

FORBIDDEN_SUBSTRINGS = ("qt6", "qt.conf", ".pdb", ".log", ".venv", ".env", ".key", ".pem")
PACKAGE_FILES = ("QuickLaunch.exe", "plugin.json", "README.md", "LICENSE", "BUILD-INFO.json")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest().lower()


def resolve_git_commit(repo_root: Path) -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(repo_root),
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=5,
        )
        return out.strip()
    except Exception:
        return "unknown"


def verify_zip_clean(archive_path: Path) -> None:
    with zipfile.ZipFile(archive_path, "r") as zf:
        names = zf.namelist()
        if set(names) != set(PACKAGE_FILES):
            raise RuntimeError(
                f"Unexpected archive entries in {archive_path.name}: {sorted(names)}"
            )
        for entry in names:
            low = entry.lower()
            for bad in FORBIDDEN_SUBSTRINGS:
                if bad in low:
                    raise RuntimeError(f"Forbidden artifact '{bad}' found in archive: {entry}")


def build_package(build_dir: Path, out_dir: Path, repo_root: Path) -> Path:
    manifest_path = repo_root / "plugin.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    errors = validate_manifest(manifest)
    if errors:
        raise RuntimeError("plugin.json validation failed:\n" + "\n".join(errors))

    plugin_id = manifest["id"]
    version = manifest["version"]
    program_name = manifest["runtime"]["program"]
    exe_src = build_dir / program_name
    if not exe_src.is_file():
        raise FileNotFoundError(f"Built executable not found: {exe_src}")

    out_dir.mkdir(parents=True, exist_ok=True)
    stage_dir = out_dir / plugin_id
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True, exist_ok=True)

    for rel in ("plugin.json", "README.md", "LICENSE"):
        src = repo_root / rel
        if not src.is_file():
            raise FileNotFoundError(f"Required package file missing: {src}")
        shutil.copy2(src, stage_dir / rel)
    shutil.copy2(exe_src, stage_dir / program_name)

    build_info = {
        "id": plugin_id,
        "version": version,
        "program": program_name,
        "qt": manifest["runtime"]["qt"],
        "qtVersion": manifest["runtime"]["qtVersion"],
        "gitCommit": resolve_git_commit(repo_root),
        "gitRef": os.environ.get("GITHUB_REF", ""),
        "builtAtUtc": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "exeSha256": sha256_file(exe_src),
        "exeSizeBytes": exe_src.stat().st_size,
    }
    build_info_text = json.dumps(build_info, ensure_ascii=False, indent=2) + "\n"
    (stage_dir / "BUILD-INFO.json").write_text(build_info_text, encoding="utf-8", newline="\n")
    (out_dir / "BUILD-INFO.json").write_text(build_info_text, encoding="utf-8", newline="\n")

    archive_name = f"{plugin_id}-{version}.wsplugin"
    archive_path = out_dir / archive_name
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for fname in PACKAGE_FILES:
            zf.write(stage_dir / fname, arcname=fname)

    verify_zip_clean(archive_path)
    archive_sha = sha256_file(archive_path)
    sha_path = out_dir / f"{archive_name}.sha256"
    sha_path.write_text(f"{archive_sha}  {archive_name}\n", encoding="utf-8", newline="\n")

    print(f"[package] Created {archive_path} ({archive_path.stat().st_size} bytes)")
    print(f"[package] SHA-256: {archive_sha} -> {sha_path}")
    return archive_path


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Package WinShun .wsplugin archive")
    parser.add_argument("--build", required=True, help="CMake build output directory")
    parser.add_argument("--out", default="dist", help="Output directory (default: dist)")
    args = parser.parse_args()

    build_dir = Path(args.build).resolve()
    out_dir = Path(args.out)
    if not out_dir.is_absolute():
        out_dir = (repo_root / out_dir).resolve()
    build_package(build_dir, out_dir, repo_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
