#!/usr/bin/env python3
"""Validate WinShun plugin.json manifest using Python standard library only."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

OFFICIAL_SCHEMA_URL = (
    "https://raw.githubusercontent.com/LingCore/WinShun-plugins/main/schema/plugin.schema.json"
)
ID_PATTERN = re.compile(r"^[a-z0-9_-]([a-z0-9._-]*[a-z0-9_-])?$")
SEMVER_PATTERN = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")

VALID_PERMISSIONS = frozenset({
    "files:read",
    "files:write",
    "processes",
    "clipboard:read",
    "clipboard:write",
    "ui",
    "network",
    "network:listen",
    "notifications",
    "system:settings",
    "winshun:search",
    "winshun:open",
    "winshun:history",
    "winshun:content",
})

VALID_SETTING_TYPES = frozenset({
    "text",
    "password",
    "bool",
    "number",
    "choice",
    "folder",
    "file",
    "hotkey",
})


def is_valid_text(value: Any) -> bool:
    if isinstance(value, str):
        return bool(value.strip())
    if isinstance(value, dict):
        return any(
            isinstance(value.get(lang), str) and bool(value[lang].strip())
            for lang in ("zh", "en")
        )
    return False


def validate_permissions(perms: Any, errors: list[str]) -> None:
    if not isinstance(perms, list):
        errors.append("'permissions' must be an array")
        return
    for idx, entry in enumerate(perms):
        if isinstance(entry, str):
            if entry == "elevated":
                errors.append(f"permissions[{idx}]: 'elevated' is forbidden with shared Qt 6.12")
            elif entry not in VALID_PERMISSIONS:
                errors.append(f"permissions[{idx}]: unknown permission '{entry}'")
        elif isinstance(entry, dict) and entry:
            for perm_key, scope in entry.items():
                if perm_key == "elevated":
                    errors.append(f"permissions[{idx}]: 'elevated' is forbidden")
                elif perm_key not in VALID_PERMISSIONS:
                    errors.append(f"permissions[{idx}]: unknown scoped permission '{perm_key}'")
                if not (isinstance(scope, str) or (isinstance(scope, list) and all(isinstance(s, str) for s in scope))):
                    errors.append(f"permissions[{idx}]['{perm_key}'] scope must be string or string[]")
        else:
            errors.append(f"permissions[{idx}] must be a permission string or non-empty scoped object")


def validate_commands(commands: Any, errors: list[str]) -> None:
    if not isinstance(commands, list) or len(commands) < 1:
        errors.append("'commands' must be a non-empty array with at least 1 command")
        return
    for idx, cmd in enumerate(commands):
        if not isinstance(cmd, dict):
            errors.append(f"commands[{idx}] must be an object")
            continue
        if not isinstance(cmd.get("id"), str) or not cmd["id"]:
            errors.append(f"commands[{idx}].id must be a non-empty string")
        if "title" in cmd and not is_valid_text(cmd["title"]):
            errors.append(f"commands[{idx}].title must be valid text")

    first = commands[0]
    if isinstance(first, dict):
        actions = first.get("actions")
        expected_keys = ["Enter", "Ctrl+Enter", "Shift+Enter"]
        if not isinstance(actions, list) or len(actions) < len(expected_keys):
            errors.append("commands[0].actions must define Enter, Ctrl+Enter, Shift+Enter in order")
            return
        for idx, act in enumerate(actions):
            if not isinstance(act, dict) or not isinstance(act.get("id"), str) or not act["id"]:
                errors.append(f"commands[0].actions[{idx}].id must be a non-empty string")
            if "title" in act and not is_valid_text(act["title"]):
                errors.append(f"commands[0].actions[{idx}].title must be valid text")
        actual_keys = [a.get("key") if isinstance(a, dict) else None for a in actions[:3]]
        if actual_keys != expected_keys:
            errors.append(f"commands[0].actions keys must be {expected_keys}, got {actual_keys}")


def validate_settings(settings: Any, errors: list[str]) -> None:
    if settings is None:
        return
    if not isinstance(settings, list):
        errors.append("'settings' must be an array")
        return
    seen_keys: set[str] = set()
    for idx, item in enumerate(settings):
        if not isinstance(item, dict):
            errors.append(f"settings[{idx}] must be an object")
            continue
        key = item.get("key")
        if not isinstance(key, str) or not key:
            errors.append(f"settings[{idx}].key must be a non-empty string")
        elif key in seen_keys:
            errors.append(f"settings[{idx}]: duplicate key '{key}'")
        else:
            seen_keys.add(key)

        stype = item.get("type")
        if stype not in VALID_SETTING_TYPES:
            errors.append(f"settings[{idx}] ('{key}'): invalid type '{stype}'")
            continue
        if "title" in item and not is_valid_text(item["title"]):
            errors.append(f"settings[{idx}] ('{key}'): invalid title")

        has_default = "default" in item
        default = item.get("default")
        if stype == "bool":
            if has_default and not isinstance(default, bool):
                errors.append(f"settings[{idx}] ('{key}'): bool default must be boolean")
        elif stype == "number":
            if has_default and (isinstance(default, bool) or not isinstance(default, (int, float))):
                errors.append(f"settings[{idx}] ('{key}'): number default must be numeric")
        elif stype == "choice":
            options = item.get("options")
            if not isinstance(options, list) or not options:
                errors.append(f"settings[{idx}] ('{key}'): choice requires non-empty 'options'")
                continue
            values = []
            for oidx, opt in enumerate(options):
                if isinstance(opt, str):
                    values.append(opt)
                elif isinstance(opt, dict) and isinstance(opt.get("value"), str) and opt["value"]:
                    if "title" in opt and not is_valid_text(opt["title"]):
                        errors.append(f"settings[{idx}].options[{oidx}] invalid title")
                    values.append(opt["value"])
                else:
                    errors.append(f"settings[{idx}].options[{oidx}] must be string or {{value, title}}")
            if has_default and default not in values:
                errors.append(f"settings[{idx}] ('{key}'): default '{default}' not in options {values}")
        else:
            if has_default and not isinstance(default, str):
                errors.append(f"settings[{idx}] ('{key}'): {stype} default must be string")


def validate_manifest(data: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(data, dict):
        return ["Root manifest JSON must be an object"]

    if data.get("$schema") != OFFICIAL_SCHEMA_URL:
        errors.append(f"'$schema' must equal '{OFFICIAL_SCHEMA_URL}'")

    manifest_ver = data.get("manifest")
    if type(manifest_ver) is not int or manifest_ver != 1:
        errors.append("'manifest' must be integer 1")

    plugin_id = data.get("id")
    if not isinstance(plugin_id, str) or not plugin_id or len(plugin_id) > 64 or not ID_PATTERN.match(plugin_id):
        errors.append("'id' must be 1..64 chars matching ^[a-z0-9_-]([a-z0-9._-]*[a-z0-9_-])?$")

    if not is_valid_text(data.get("name")):
        errors.append("'name' must be a non-empty string or localized {zh, en} object")

    version = data.get("version")
    if not isinstance(version, str) or not SEMVER_PATTERN.match(version):
        errors.append("'version' must be a semantic version string like 0.1.0")

    runtime = data.get("runtime")
    if not isinstance(runtime, dict):
        errors.append("'runtime' must be an object")
    else:
        if runtime.get("mode") != "resident":
            errors.append("runtime.mode must be 'resident'")
        if runtime.get("program") != "QuickLaunch.exe":
            errors.append("runtime.program must be 'QuickLaunch.exe'")
        if runtime.get("qt") != "host":
            errors.append("runtime.qt must be 'host'")
        if runtime.get("qtVersion") != "6.12.0":
            errors.append("runtime.qtVersion must be '6.12.0'")

    validate_permissions(data.get("permissions", []), errors)
    validate_commands(data.get("commands"), errors)
    validate_settings(data.get("settings"), errors)
    return errors


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Validate WinShun plugin.json")
    parser.add_argument(
        "manifest",
        nargs="?",
        default=str(repo_root / "plugin.json"),
        help="Path to plugin.json (default: <repo>/plugin.json)",
    )
    args = parser.parse_args()
    manifest_path = Path(args.manifest).resolve()
    try:
        raw = manifest_path.read_text(encoding="utf-8")
        data = json.loads(raw)
    except Exception as exc:
        print(f"[FAIL] Cannot read/parse {manifest_path}: {exc}", file=sys.stderr)
        return 1

    errors = validate_manifest(data)
    if errors:
        print(f"[FAIL] {manifest_path} failed validation ({len(errors)} error(s)):", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print(f"[OK] Manifest valid: {manifest_path} (id={data['id']}, version={data['version']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
