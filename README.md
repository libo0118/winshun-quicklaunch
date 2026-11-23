# Win顺 快速启动面板（`libo0118.quicklaunch`）

仿 **MayeNano** 风格的 Win顺 原生 C++20 / Qt 6.12 QML 快速启动面板插件（灵感来自 [25H/MayeNano](https://github.com/25H/MayeNano)）：按下全局快捷键（默认 `Alt+1`）随时呼出或收起弹出面板，集中管理常用程序、目录与网址，支持即输即搜、拼音首字母检索、批量拖拽录入与一键导入 MayeNano `MDB.json`。

## 核心功能

- **全局热键呼出**：默认 `Alt+1` 一键弹出/收起，在 `hostHotkeys` 能力可用时先 `winshun/hotkeys` 声明让出并调用 `register_hotkeys`；旧版宿主自动回退 Win32 `RegisterHotKey`（含 300 ms 重试）。
- **分组与双视图**：左侧分组栏支持增删改与排序；支持图标网格（小/中/大）与紧凑列表切换；支持 `Ctrl+1~9` 速启当前视图前 9 项。
- **拖拽与快捷添加**：从资源管理器拖入 `.exe`、`.lnk`、文件或文件夹（自动解析 `.lnk` 目标、参数、工作目录与管理员标记），也可通过资源管理器右键菜单「添加到快速启动面板」批量录入。
- **即输即搜与全键盘**：呼出自动聚焦搜索框，支持中文、英文、拼音首字母、别名、路径多词过滤；方向键移动、`Enter` 启动、`Ctrl+Enter` 定位目录、`Shift+Enter` 管理员运行、`Ctrl+Tab` 切换分组、`Esc` 清空/隐藏。
- **一键导入 MayeNano**：兼容导入现有 `MDB.json`，保留分组、`%mp%` / `%mr%` 变量、`param` 启动参数、`dir` 工作目录及 `$computer` / `$controls` 系统项。

## 关键字与示例

在 Win顺 搜索框输入 `ql`、`maye` 或 `启动面板` 加空格：
- **留空**：首项为「打开快速启动面板（Alt+1）」，下方列出常用条目（`Enter` 启动、`Ctrl+Enter` 打开所在文件夹、`Shift+Enter` 打开面板）。
- **输入关键词或拼音**：例如 `ql jsb` 或 `ql 记事本`（纯只读查询，不会触发任何启动副作用）。

## 运行依赖与共享 Qt 6.12 契约

- 本插件通过 `"qt": "host", "qtVersion": "6.12.0"` 复用 Win顺 宿主自带的 Qt 6.12（`Qt6Core`、`Qt6Gui`、`Qt6Qml`、`Qt6Quick`、`QtQuick.Controls.Basic`、`QtQuick.Layouts`），**包内不附带任何 Qt DLL 或 `qt.conf`**，免安装额外运行时。
- 支持普通权限 MSVC x64 Release，不与 `elevated` 同用（最低 Win顺 `0.25.0`）。

## 权限说明

1. **`ui`**：创建并显示独立 QML 弹出面板，接收 `appearance` 主题，以及调用 `register_hotkeys` 注册 `Alt+1`。
2. **`processes`**：允许用户主动启动外部程序或带参数/工作目录的快捷方式（不申请 `elevated`）。
3. **`winshun:open`**：通过 `winshun/tool` 的 `open_item`、`open_url`、`show_in_folder` 在插件作业对象之外启动目标程序、网址与目录，确保目标程序不受插件 256 MB 内存上限约束且不随插件退出而终止。

## 数据位置

数据均写入 Win顺 分配的 `WS_DATA_DIR`（如 `%LOCALAPPDATA%\WinShun\plugins\libo0118.quicklaunch.data` 或便携版 `data\plugins\libo0118.quicklaunch.data`）：
- `launcher-db.json`：分组、条目与窗口尺寸（`QSaveFile` 原子写；损坏时自动备份为 `launcher-db.corrupt.*.bak` 并恢复默认项）。
- `launchers/*.lnk`：带启动参数、工作目录、管理员标志或系统特殊命令的快捷方式入口。

## GitHub Actions CI 构建与安装

推送代码到 GitHub 后，`.github/workflows/build.yml` 会在 `windows-2022` 上自动：
1. 下载并校验 SHA-256 固定的 Qt `6.12.0` MSVC2022 x64 开发包（仅编译时使用）；
2. 使用 MSVC x64 Release 编译 `QuickLaunch.exe` 与 `quicklaunch_tests.exe`；
3. 运行 C++ 单元测试、`plugin.json` Schema 校验与真实 JSON-RPC 管道端到端测试；
4. 打包生成不含任何 Qt DLL 的 `libo0118.quicklaunch-<version>.wsplugin` 及 `.sha256`（打 `v*` tag 自动发布 GitHub Release）。

下载 `.wsplugin` 后安装启用：
```powershell
ws plugin install .\libo0118.quicklaunch-0.2.0.wsplugin
ws plugin enable libo0118.quicklaunch
```

## 失败处理与许可证

- 热键冲突（如错误码 1409）会在底部状态栏与设置弹窗明确告警；目标缺失会返回可读错误而非伪造成功。
- 基于 **MIT License** 开源，详见 [LICENSE](LICENSE)。
