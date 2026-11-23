#!/usr/bin/env python3
"""Real JSON-RPC 2.0 end-to-end test driver for QuickLaunch.exe."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any


class JsonRpcHarness:
    def __init__(self, exe_path: Path, qt_dir: Path, plugin_dir: Path, data_dir: Path) -> None:
        env = os.environ.copy()
        qt_bin = str(qt_dir / "bin")
        env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")
        env["QT_PLUGIN_PATH"] = str(qt_dir / "plugins")
        env["QML_IMPORT_PATH"] = str(qt_dir / "qml")
        env["WS_QT_DIR"] = str(qt_dir)
        env["WS_QT_QML_DIR"] = str(qt_dir / "qml")
        env["QT_QUICK_CONTROLS_STYLE"] = "Basic"
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        env["WS_PLUGIN_DIR"] = str(plugin_dir)
        env["WS_DATA_DIR"] = str(data_dir)

        self.proc = subprocess.Popen(
            [str(exe_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=str(plugin_dir),
            env=env,
        )
        self._stdin_lock = threading.Lock()
        self._cv = threading.Condition()
        self._next_id = 1
        self.responses: dict[Any, dict[str, Any]] = {}
        self.events: list[dict[str, Any]] = []
        self.stdout_errors: list[str] = []
        self.stderr_lines: list[str] = []
        self.eof = False

        self._out_thread = threading.Thread(target=self._read_stdout, daemon=True)
        self._err_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self._out_thread.start()
        self._err_thread.start()

    def _send_raw(self, payload: dict[str, Any]) -> None:
        assert self.proc.stdin is not None
        raw = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        with self._stdin_lock:
            self.proc.stdin.write(raw)
            self.proc.stdin.flush()

    def _handle_incoming(self, msg: dict[str, Any]) -> None:
        method = msg.get("method")
        if method == "winshun/tool" and "id" in msg and msg["id"] is not None:
            tool_name = msg.get("params", {}).get("name", "")
            result: dict[str, Any] = {"errors": {}} if tool_name == "register_hotkeys" else {"ok": True}
            self._send_raw({"jsonrpc": "2.0", "id": msg["id"], "result": result})

        with self._cv:
            if not method and "id" in msg:
                self.responses[msg["id"]] = msg
            else:
                self.events.append(msg)
            self._cv.notify_all()

    def _read_stdout(self) -> None:
        assert self.proc.stdout is not None
        while True:
            raw = self.proc.stdout.readline()
            if not raw:
                break
            if raw.startswith(b"\xef\xbb\xbf"):
                self.stdout_errors.append(f"UTF-8 BOM detected on stdout: {raw[:32]!r}")
                continue
            try:
                text = raw.decode("utf-8").strip()
            except UnicodeDecodeError as exc:
                self.stdout_errors.append(f"Invalid UTF-8 on stdout: {exc} ({raw!r})")
                continue
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError as exc:
                self.stdout_errors.append(f"Non-JSON line on stdout: {text!r} ({exc})")
                continue
            if not isinstance(obj, dict):
                self.stdout_errors.append(f"Non-object JSON on stdout: {text!r}")
                continue
            self._handle_incoming(obj)
        with self._cv:
            self.eof = True
            self._cv.notify_all()

    def _read_stderr(self) -> None:
        assert self.proc.stderr is not None
        for raw in self.proc.stderr:
            self.stderr_lines.append(raw.decode("utf-8", errors="replace").rstrip())

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        self._send_raw({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def request(self, method: str, params: dict[str, Any] | None = None, timeout: float = 5.0) -> Any:
        req_id = self._next_id
        self._next_id += 1
        self._send_raw({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params or {}})
        deadline = time.monotonic() + timeout
        with self._cv:
            while req_id not in self.responses:
                if self.eof:
                    raise AssertionError(f"Process closed stdout before replying to {method}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for response to {method}")
                self._cv.wait(remaining)
            msg = self.responses.pop(req_id)
        if "error" in msg:
            raise AssertionError(f"RPC {method} returned error: {msg['error']}")
        return msg.get("result")

    def event_cursor(self) -> int:
        with self._cv:
            return len(self.events)

    def wait_for_hotkey_pair(self, cursor: int, expected_keys: list[str], timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        with self._cv:
            while True:
                slice_events = self.events[cursor:]
                saw_reserved = any(
                    ev.get("method") == "winshun/hotkeys"
                    and ev.get("params", {}).get("keys") == expected_keys
                    for ev in slice_events
                )
                saw_tool = any(
                    ev.get("method") == "winshun/tool"
                    and ev.get("params", {}).get("name") == "register_hotkeys"
                    and ev.get("params", {}).get("arguments", {}).get("keys") == expected_keys
                    for ev in slice_events
                )
                if saw_reserved and saw_tool:
                    return
                if self.eof:
                    raise AssertionError(
                        f"EOF before observing hotkeys={expected_keys}; events={slice_events}"
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(
                        f"Timed out waiting for hotkeys={expected_keys}; got={slice_events}"
                    )
                self._cv.wait(remaining)


def run_e2e(exe_path: Path, qt_dir: Path, repo_root: Path) -> None:
    readme_path = repo_root / "README.md"
    if not readme_path.is_file():
        raise FileNotFoundError(f"Missing README.md at {readme_path}")

    with tempfile.TemporaryDirectory(prefix="ql-e2e-") as tmp:
        data_dir = Path(tmp) / "data"
        data_dir.mkdir(parents=True, exist_ok=True)
        harness = JsonRpcHarness(exe_path, qt_dir, repo_root, data_dir)
        try:
            init_res = harness.request("initialize", {
                "protocolVersion": "1",
                "winshun": "0.25.0",
                "pluginDir": str(repo_root),
                "dataDir": str(data_dir),
                "locale": "zh",
                "theme": "dark",
                "capabilities": {"hostHotkeys": True},
                "appearance": {"locale": "zh", "theme": "dark", "corners": "round"},
                "settings": {"popupHotkey": "Alt+1"},
            })
            assert isinstance(init_res, dict) and init_res.get("protocolVersion") == "1", init_res
            harness.wait_for_hotkey_pair(0, ["Alt+1"])

            db_file = data_dir / "launcher-db.json"
            before_db = db_file.read_bytes() if db_file.is_file() else None
            q_params = {
                "queryId": 1, "command": "ql", "trigger": "keyword",
                "keyword": "ql", "text": "", "raw": "ql ", "limit": 20,
            }
            q_empty = harness.request("query", q_params)
            items = q_empty.get("items", [])
            assert len(items) == 5, f"Expected __open_panel__ + 4 default items, got {len(items)}"
            assert items[0].get("id") == "__open_panel__", items[0]
            assert all(isinstance(it.get("id"), str) and it["id"] for it in items)
            assert all(any("\u4e00" <= c <= "\u9fff" for c in it.get("title", "")) for it in items)
            q_empty_2 = harness.request("query", q_params)
            assert [it["id"] for it in q_empty_2.get("items", [])] == [it["id"] for it in items]
            assert (db_file.read_bytes() if db_file.is_file() else None) == before_db
            assert not any(
                ev.get("method") == "winshun/tool"
                and ev.get("params", {}).get("name") in ("open_item", "open_url", "show_in_folder")
                for ev in harness.events
            )

            for idx, term in enumerate(("记事本", "jsb"), start=2):
                res = harness.request("query", {
                    "queryId": idx, "command": "ql", "trigger": "keyword",
                    "keyword": "ql", "text": term, "raw": f"ql {term}", "limit": 20,
                })
                assert any("记事本" in it.get("title", "") for it in res.get("items", [])), (term, res)

            res_miss = harness.request("query", {
                "queryId": 4, "command": "ql", "trigger": "keyword",
                "keyword": "ql", "text": "___not_found_xyz___",
                "raw": "ql ___not_found_xyz___", "limit": 20,
            })
            miss_items = res_miss.get("items", [])
            assert len(miss_items) >= 1 and miss_items[0].get("valid") is False, res_miss

            act_unknown = harness.request("action", {
                "command": "ql", "action": "launch",
                "item": {"id": "__nonexistent_item__", "title": "不存在"},
                "text": "missing",
            })
            eff_unknown = act_unknown.get("effects", [])
            assert any(e.get("type") == "notify" and e.get("text") for e in eff_unknown), eff_unknown
            assert not any(e.get("type") in ("open", "reveal") for e in eff_unknown), eff_unknown

            act_files = harness.request("action", {
                "command": "ql", "action": "files", "trigger": "files",
                "files": [str(readme_path)], "item": {}, "text": "",
            })
            eff_files = act_files.get("effects", [])
            assert any(e.get("type") == "notify" and e.get("text") for e in eff_files), eff_files
            assert db_file.is_file(), "Expected launcher-db.json to be written"
            db_data = json.loads(db_file.read_text(encoding="utf-8"))
            assert any(Path(it.get("target", "")).name.lower() == "readme.md" for it in db_data.get("items", []))

            ghost_file = Path(tmp) / "ghost_target_check.txt"
            ghost_file.write_text("temp", encoding="utf-8")
            harness.request("action", {
                "command": "ql", "action": "files", "trigger": "files",
                "files": [str(ghost_file)], "item": {}, "text": "",
            })
            ghost_file.unlink()
            ghost_query = harness.request("query", {
                "queryId": 5, "command": "ql", "trigger": "keyword",
                "keyword": "ql", "text": "ghost_target_check",
                "raw": "ql ghost_target_check", "limit": 10,
            })
            ghost_items = ghost_query.get("items", [])
            assert ghost_items and ghost_items[0].get("id") != "__no_match__", ghost_query
            ghost_act = harness.request("action", {
                "command": "ql", "action": "launch", "item": ghost_items[0], "text": "ghost",
            })
            eff_ghost = ghost_act.get("effects", [])
            assert any(e.get("type") == "notify" and "不存在" in e.get("text", "") for e in eff_ghost), eff_ghost
            assert not any(e.get("type") in ("open", "reveal") for e in eff_ghost), eff_ghost

            tray = harness.request("trayMenu", {})
            tray_items = tray.get("items", [])
            assert len(tray_items) >= 1 and any("Alt+1" in it.get("title", "") for it in tray_items), tray

            cur = harness.event_cursor()
            harness.notify("settingsChanged", {"settings": {"popupHotkey": "Alt+2"}})
            harness.wait_for_hotkey_pair(cur, ["Alt+2"])

            cur = harness.event_cursor()
            harness.notify("settingsChanged", {"settings": {"popupHotkey": ""}})
            harness.wait_for_hotkey_pair(cur, [])

            harness.notify("appearance", {"locale": "en", "theme": "light", "corners": "square"})
            harness.notify("appearance", {
                "locale": "zh", "theme": "dark", "corners": "round",
                "nativeLook": {"dark": True, "rounded": True, "cardRadius": 10, "controlRadius": 6},
            })
            alive = harness.request("query", q_params)
            assert len(alive.get("items", [])) >= 5

            panel_act = harness.request("action", {
                "command": "ql", "action": "open-panel",
                "item": {"id": "__open_panel__"}, "text": "",
            })
            assert any(e.get("type") == "hide" for e in panel_act.get("effects", [])), panel_act
            time.sleep(0.5)

            cur = harness.event_cursor()
            shut_res = harness.request("shutdown", {}, timeout=3.0)
            assert shut_res == {}, shut_res
            harness.wait_for_hotkey_pair(cur, [], timeout=3.0)
            exit_code = harness.proc.wait(timeout=3.0)
            assert exit_code == 0, f"Expected exit code 0, got {exit_code}"
            assert not harness.stdout_errors, f"Stdout protocol violations: {harness.stdout_errors}"
            qml_errors = [
                ln for ln in harness.stderr_lines
                if "Failed to load qrc:/" in ln or "QQmlApplicationEngine failed" in ln
                or "Invalid property assignment" in ln or "Component is not ready" in ln
            ]
            assert not qml_errors, f"QML loading errors on stderr: {qml_errors}"
            print("[OK] E2E JSON-RPC test suite passed (12/12 checks, incl. QML load)")
        except Exception:
            if harness.stderr_lines:
                print("--- QuickLaunch stderr ---", file=sys.stderr)
                for line in harness.stderr_lines:
                    print(line, file=sys.stderr)
            raise
        finally:
            if harness.proc.poll() is None:
                harness.proc.kill()
                harness.proc.wait(timeout=3.0)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Run E2E JSON-RPC test against QuickLaunch.exe")
    parser.add_argument("--exe", required=True, help="Path to QuickLaunch.exe")
    parser.add_argument("--qt-dir", required=True, help="Path to Qt 6.12.0 MSVC2022 64-bit prefix")
    args = parser.parse_args()
    run_e2e(Path(args.exe).resolve(), Path(args.qt_dir).resolve(), repo_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
