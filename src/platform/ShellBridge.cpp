#include "platform/ShellBridge.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QWindow>

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <vector>

namespace ql {
namespace {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** put()
    {
        reset();
        return &m_ptr;
    }
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    void reset()
    {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr = nullptr;
};

struct ComInitGuard {
    bool needUninit = false;
    ComInitGuard()
    {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        needUninit = (hr == S_OK || hr == S_FALSE);
    }
    ~ComInitGuard()
    {
        if (needUninit)
            ::CoUninitialize();
    }
};

struct CoTaskStrGuard {
    PWSTR ptr = nullptr;
    ~CoTaskStrGuard()
    {
        if (ptr)
            ::CoTaskMemFree(ptr);
    }
};

struct PidlGuard {
    LPITEMIDLIST ptr = nullptr;
    ~PidlGuard()
    {
        if (ptr)
            ::CoTaskMemFree(ptr);
    }
};
QString stripQuotes(const QString& s)
{
    QString t = s.trimmed();
    if (t.size() >= 2 && t.startsWith(u'"') && t.endsWith(u'"'))
        return t.mid(1, t.size() - 2).trimmed();
    return t;
}

QString searchInDir(const QString& dir, const QStringList& candidates)
{
    if (dir.isEmpty())
        return {};
    const QDir base(dir);
    for (const QString& name : candidates) {
        const QString full = base.filePath(name);
        const QFileInfo fi(full);
        if (fi.exists() && fi.isFile())
            return QDir::toNativeSeparators(fi.absoluteFilePath());
    }
    return {};
}

QString searchInPathEnv(const QStringList& candidates)
{
    for (const QString& name : candidates) {
        const auto nameW = name.toStdWString();
        const DWORD need = ::SearchPathW(nullptr, nameW.c_str(), nullptr, 0, nullptr, nullptr);
        if (need == 0)
            continue;
        std::vector<wchar_t> buf(need + 1, L'\0');
        const DWORD len = ::SearchPathW(nullptr, nameW.c_str(), nullptr, need + 1, buf.data(), nullptr);
        if (len > 0 && len <= need) {
            const QString found = QString::fromWCharArray(buf.data(), static_cast<int>(len));
            if (QFileInfo::exists(found))
                return QDir::toNativeSeparators(found);
        }
    }
    return {};
}

QString queryAppPathsKey(HKEY root, const QString& subKey)
{
    HKEY hKey = nullptr;
    const auto subW = subKey.toStdWString();
    if (::RegOpenKeyExW(root, subW.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return {};
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG q1 = ::RegQueryValueExW(hKey, nullptr, nullptr, &type, nullptr, &bytes);
    QString out;
    if (q1 == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 2, L'\0');
        DWORD size = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (::RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &size) == ERROR_SUCCESS) {
            const QString raw = stripQuotes(QString::fromWCharArray(buf.data()));
            const QString expanded = ShellBridge::expandPath(raw);
            if (!expanded.isEmpty() && QFileInfo::exists(expanded))
                out = QDir::toNativeSeparators(expanded);
        }
    }
    ::RegCloseKey(hKey);
    return out;
}

QString sanitizeSafeId(const QString& id)
{
    QString out;
    out.reserve(id.size());
    for (const QChar ch : id) {
        if (ch.isLetterOrNumber() || ch == u'_' || ch == u'-')
            out.append(ch);
        else
            out.append(u'_');
    }
    return out.isEmpty() ? QStringLiteral("item") : out;
}

} // namespace
QString ShellBridge::expandPath(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return {};
    const auto w = trimmed.toStdWString();
    const DWORD needed = ::ExpandEnvironmentStringsW(w.c_str(), nullptr, 0);
    if (needed == 0)
        return trimmed;
    std::vector<wchar_t> buf(needed + 1, L'\0');
    const DWORD written = ::ExpandEnvironmentStringsW(w.c_str(), buf.data(), needed + 1);
    if (written == 0)
        return trimmed;
    return QString::fromWCharArray(buf.data());
}

bool ShellBridge::isWebUrl(const QString& target)
{
    const QString t = target.trimmed();
    return t.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || t.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive);
}

bool ShellBridge::isSpecialSystemUri(const QString& target)
{
    const QString t = target.trimmed();
    return t.compare(QLatin1String("$computer"), Qt::CaseInsensitive) == 0
        || t.compare(QLatin1String("$controls"), Qt::CaseInsensitive) == 0;
}

QString ShellBridge::resolveFullPath(const QString& target)
{
    const QString t = stripQuotes(expandPath(target));
    if (t.isEmpty() || isWebUrl(t) || isSpecialSystemUri(t))
        return t;

    const QFileInfo fi(t);
    if (fi.isAbsolute())
        return QDir::toNativeSeparators(t);
    if (t.contains(u'/') || t.contains(u'\\'))
        return fi.exists() ? QDir::toNativeSeparators(fi.absoluteFilePath()) : QDir::toNativeSeparators(t);

    QStringList candidates{t};
    if (!t.contains(u'.')) {
        candidates.append(t + QStringLiteral(".exe"));
        candidates.append(t + QStringLiteral(".cmd"));
        candidates.append(t + QStringLiteral(".bat"));
        candidates.append(t + QStringLiteral(".com"));
        candidates.append(t + QStringLiteral(".lnk"));
    }

    wchar_t sysBuf[MAX_PATH]{};
    const UINT sysLen = ::GetSystemDirectoryW(sysBuf, MAX_PATH);
    if (sysLen > 0 && sysLen < MAX_PATH) {
        if (const QString found = searchInDir(QString::fromWCharArray(sysBuf, static_cast<int>(sysLen)), candidates); !found.isEmpty())
            return found;
    }

    wchar_t winBuf[MAX_PATH]{};
    const UINT winLen = ::GetWindowsDirectoryW(winBuf, MAX_PATH);
    if (winLen > 0 && winLen < MAX_PATH) {
        if (const QString found = searchInDir(QString::fromWCharArray(winBuf, static_cast<int>(winLen)), candidates); !found.isEmpty())
            return found;
    }

    if (const QString found = searchInPathEnv(candidates); !found.isEmpty())
        return found;

    QStringList appNames{t};
    if (!t.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        appNames.append(t + QStringLiteral(".exe"));
    for (const QString& appName : appNames) {
        const QString subKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + appName;
        if (const QString found = queryAppPathsKey(HKEY_CURRENT_USER, subKey); !found.isEmpty())
            return found;
        if (const QString found = queryAppPathsKey(HKEY_LOCAL_MACHINE, subKey); !found.isEmpty())
            return found;
    }
    return t;
}
bool ShellBridge::existsOrResolvable(const QString& target)
{
    if (isWebUrl(target) || isSpecialSystemUri(target))
        return true;
    const QString resolved = resolveFullPath(target);
    return !resolved.isEmpty() && QFileInfo::exists(resolved);
}

std::optional<ResolvedShortcut> ShellBridge::resolveLnk(const QString& lnkPath)
{
    const QString path = stripQuotes(expandPath(lnkPath));
    if (path.isEmpty() || !QFileInfo::exists(path))
        return std::nullopt;

    ComInitGuard com;
    ComPtr<IShellLinkW> link;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()))))
        return std::nullopt;

    ComPtr<IPersistFile> pf;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(pf.put()))))
        return std::nullopt;

    const auto lnkW = QDir::toNativeSeparators(path).toStdWString();
    if (FAILED(pf->Load(lnkW.c_str(), STGM_READ)))
        return std::nullopt;

    ResolvedShortcut out;
    wchar_t buf[4096]{};
    if (link->GetPath(buf, 4096, nullptr, 0) != S_OK || buf[0] == L'\0') {
        buf[0] = L'\0';
        if (link->GetPath(buf, 4096, nullptr, SLGP_RAWPATH) != S_OK || buf[0] == L'\0') {
            buf[0] = L'\0';
            PidlGuard pidl;
            if (SUCCEEDED(link->GetIDList(&pidl.ptr)) && pidl.ptr) {
                if (!::SHGetPathFromIDListW(pidl.ptr, buf))
                    buf[0] = L'\0';
            }
        }
    }
    out.target = expandPath(QString::fromWCharArray(buf));
    if (out.target.isEmpty())
        return std::nullopt;
    out.target = QDir::toNativeSeparators(out.target);

    buf[0] = L'\0';
    if (SUCCEEDED(link->GetArguments(buf, 4096)))
        out.arguments = QString::fromWCharArray(buf).trimmed();

    buf[0] = L'\0';
    if (SUCCEEDED(link->GetWorkingDirectory(buf, 4096))) {
        const QString dir = expandPath(QString::fromWCharArray(buf));
        if (!dir.isEmpty())
            out.workingDir = QDir::toNativeSeparators(dir);
    }

    buf[0] = L'\0';
    int iconIdx = 0;
    if (SUCCEEDED(link->GetIconLocation(buf, 4096, &iconIdx)) && buf[0] != L'\0') {
        const QString icon = expandPath(QString::fromWCharArray(buf));
        if (!icon.isEmpty()) {
            const QString nativeIcon = QDir::toNativeSeparators(icon);
            out.iconPath = (iconIdx == 0) ? nativeIcon : QStringLiteral("%1,%2").arg(nativeIcon).arg(iconIdx);
        }
    }

    ComPtr<IShellLinkDataList> dataList;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(dataList.put())))) {
        DWORD flags = 0;
        if (SUCCEEDED(dataList->GetFlags(&flags)))
            out.runAsAdmin = (flags & SLDF_RUNAS_USER) != 0;
    }
    return out;
}
QString ShellBridge::ensureLaunchLnk(
    const QString& dataDir, const LaunchItem& item, bool forceAdmin, QString* error)
{
    auto fail = [error](const QString& msg) -> QString {
        if (error)
            *error = msg;
        return {};
    };
    const QString rawTarget = item.target.trimmed();
    if (dataDir.trimmed().isEmpty() || rawTarget.isEmpty())
        return fail(QStringLiteral("缺少数据目录或启动目标"));

    QString targetPath;
    QString args = expandPath(item.arguments);
    if (rawTarget.compare(QLatin1String("$computer"), Qt::CaseInsensitive) == 0) {
        targetPath = resolveFullPath(QStringLiteral("explorer.exe"));
        args = QStringLiteral("shell:MyComputerFolder");
    } else if (rawTarget.compare(QLatin1String("$controls"), Qt::CaseInsensitive) == 0) {
        targetPath = resolveFullPath(QStringLiteral("control.exe"));
    } else {
        targetPath = resolveFullPath(rawTarget);
    }
    if (targetPath.isEmpty())
        return fail(QStringLiteral("无法解析目标路径"));

    const QString launchersDir = QDir(dataDir).filePath(QStringLiteral("launchers"));
    if (!QDir().mkpath(launchersDir))
        return fail(QStringLiteral("无法创建 launchers 目录"));
    const QString lnkPath = QDir::toNativeSeparators(
        QDir(launchersDir).filePath(sanitizeSafeId(item.id) + QStringLiteral(".lnk")));

    ComInitGuard com;
    ComPtr<IShellLinkW> link;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()))))
        return fail(QStringLiteral("创建 IShellLinkW 失败"));
    link->SetPath(targetPath.toStdWString().c_str());
    if (!args.isEmpty())
        link->SetArguments(args.toStdWString().c_str());
    const QString workDir = expandPath(item.workingDir);
    if (!workDir.isEmpty())
        link->SetWorkingDirectory(QDir::toNativeSeparators(workDir).toStdWString().c_str());

    QString iconSpec = item.iconPath.trimmed();
    if (iconSpec.startsWith(QLatin1String("custom:"), Qt::CaseInsensitive))
        iconSpec = iconSpec.mid(7).trimmed();
    if (!iconSpec.isEmpty()) {
        int iconIdx = 0;
        const qsizetype comma = iconSpec.lastIndexOf(u',');
        if (comma > 0) {
            bool ok = false;
            const int parsedIdx = iconSpec.mid(comma + 1).trimmed().toInt(&ok);
            if (ok) {
                iconIdx = parsedIdx;
                iconSpec = iconSpec.left(comma).trimmed();
            }
        }
        const QString resolvedIcon = resolveFullPath(iconSpec);
        const QString ext = QFileInfo(resolvedIcon).suffix().toLower();
        if ((ext == QLatin1String("ico") || ext == QLatin1String("exe") || ext == QLatin1String("dll"))
            && QFileInfo::exists(resolvedIcon)) {
            link->SetIconLocation(QDir::toNativeSeparators(resolvedIcon).toStdWString().c_str(), iconIdx);
        }
    }

    ComPtr<IShellLinkDataList> dataList;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(dataList.put())))) {
        DWORD flags = 0;
        if (SUCCEEDED(dataList->GetFlags(&flags))) {
            if (forceAdmin || item.runAsAdmin)
                flags |= SLDF_RUNAS_USER;
            else
                flags &= ~SLDF_RUNAS_USER;
            dataList->SetFlags(flags);
        }
    }
    ComPtr<IPersistFile> pf;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(pf.put())))
        || FAILED(pf->Save(lnkPath.toStdWString().c_str(), TRUE))) {
        return fail(QStringLiteral("保存快捷方式失败：%1").arg(lnkPath));
    }
    return lnkPath;
}
bool ShellBridge::needsShimLnk(const LaunchItem& item, bool forceAdmin)
{
    if (forceAdmin || item.runAsAdmin)
        return true;
    if (!item.arguments.trimmed().isEmpty() || !item.workingDir.trimmed().isEmpty())
        return true;
    if (isSpecialSystemUri(item.target))
        return true;
    const QString expanded = stripQuotes(expandPath(item.target));
    if (isWebUrl(expanded))
        return false;
    return !QFileInfo(expanded).isAbsolute();
}

namespace {

QString runFileDialog(IFileDialog* dlg, QWindow* owner, const QString& title,
    const QString& filterName, const QString& filterPattern,
    const QString& defaultName, const QString& initialDir, DWORD extraOptions)
{
    if (!dlg)
        return {};
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        dlg->SetOptions(opts | extraOptions);
    if (!title.isEmpty())
        dlg->SetTitle(title.toStdWString().c_str());

    if (!filterName.isEmpty() && !filterPattern.isEmpty()) {
        const auto nameW = filterName.toStdWString();
        const auto patW = filterPattern.toStdWString();
        COMDLG_FILTERSPEC specs[2]{
            {nameW.c_str(), patW.c_str()},
            {L"All Files (*.*)", L"*.*"}
        };
        const UINT count = (filterPattern.trimmed() == QLatin1String("*.*")) ? 1u : 2u;
        dlg->SetFileTypes(count, specs);
        dlg->SetFileTypeIndex(1);
    }

    if (!defaultName.isEmpty())
        dlg->SetFileName(defaultName.toStdWString().c_str());

    if (filterPattern.startsWith(QLatin1String("*.")) && !filterPattern.contains(u';')
        && filterPattern != QLatin1String("*.*")) {
        dlg->SetDefaultExtension(filterPattern.mid(2).toStdWString().c_str());
    }

    const QString dir = ShellBridge::expandPath(initialDir);
    if (!dir.isEmpty() && QDir(dir).exists()) {
        ComPtr<IShellItem> folder;
        const auto dirW = QDir::toNativeSeparators(dir).toStdWString();
        if (SUCCEEDED(::SHCreateItemFromParsingName(dirW.c_str(), nullptr, IID_PPV_ARGS(folder.put()))))
            dlg->SetFolder(folder.get());
    }

    const HWND hwndOwner = owner ? reinterpret_cast<HWND>(owner->winId()) : nullptr;
    if (FAILED(dlg->Show(hwndOwner)))
        return {};
    ComPtr<IShellItem> result;
    if (FAILED(dlg->GetResult(result.put())) || !result)
        return {};
    CoTaskStrGuard pszPath;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &pszPath.ptr)) || !pszPath.ptr)
        return {};
    return QDir::toNativeSeparators(QString::fromWCharArray(pszPath.ptr));
}

} // namespace
QString ShellBridge::pickOpenFile(
    QWindow* owner, const QString& title, const QString& filterName,
    const QString& filterPattern, const QString& initialDir)
{
    ComInitGuard com;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.put()))))
        return {};
    return runFileDialog(dlg.get(), owner, title, filterName, filterPattern, QString{}, initialDir,
        FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NODEREFERENCELINKS);
}

QString ShellBridge::pickSaveFile(
    QWindow* owner, const QString& title, const QString& filterName,
    const QString& filterPattern, const QString& defaultName, const QString& initialDir)
{
    ComInitGuard com;
    ComPtr<IFileSaveDialog> dlg;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.put()))))
        return {};
    return runFileDialog(dlg.get(), owner, title, filterName, filterPattern, defaultName, initialDir,
        FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
}

QString ShellBridge::pickFolder(QWindow* owner, const QString& title, const QString& initialDir)
{
    ComInitGuard com;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.put()))))
        return {};
    return runFileDialog(dlg.get(), owner, title, QString{}, QString{}, QString{}, initialDir,
        FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
}

void ShellBridge::applyWindowChrome(QWindow* window, const ThemeData& theme)
{
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;
    const BOOL dark = theme.dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const DWORD corner = theme.rounded ? 2 : 1;
    ::DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
    const QColor edgeColor(theme.windowEdge);
    if (edgeColor.isValid()) {
        const COLORREF borderColor = RGB(edgeColor.red(), edgeColor.green(), edgeColor.blue());
        ::DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &borderColor, sizeof(borderColor));
    }
}

void ShellBridge::positionWindow(QWindow* window, bool nearCursor)
{
    if (!window)
        return;
    POINT pt{};
    ::GetCursorPos(&pt);
    const HMONITOR mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(mon, &mi))
        return;

    const double dpr = (window->devicePixelRatio() > 0.0) ? window->devicePixelRatio() : 1.0;
    const int logicalW = window->width() > 0 ? window->width() : 760;
    const int logicalH = window->height() > 0 ? window->height() : 520;
    const LONG winW = static_cast<LONG>(qRound(logicalW * dpr));
    const LONG winH = static_cast<LONG>(qRound(logicalH * dpr));
    const RECT& work = mi.rcWork;

    LONG physX = work.left + (work.right - work.left - winW) / 2;
    LONG physY = work.top + (work.bottom - work.top - winH) / 2;
    if (nearCursor) {
        const LONG pad = static_cast<LONG>(qRound(12.0 * dpr));
        physX = (pt.x + pad + winW <= work.right) ? (pt.x + pad) : (pt.x - winW - pad);
        physY = (pt.y + pad + winH <= work.bottom) ? (pt.y + pad) : (pt.y - winH - pad);
    }

    const LONG maxX = std::max(work.left, work.right - winW);
    const LONG maxY = std::max(work.top, work.bottom - winH);
    physX = std::clamp(physX, work.left, maxX);
    physY = std::clamp(physY, work.top, maxY);
    window->setPosition(qRound(physX / dpr), qRound(physY / dpr));
}
void ShellBridge::activateWindow(QWindow* window)
{
    if (!window)
        return;
    if (!window->isVisible())
        window->show();
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        ::ShowWindow(hwnd, SW_SHOW);
        ::SetForegroundWindow(hwnd);
    }
    window->raise();
    window->requestActivate();
}

bool ShellBridge::standaloneLaunch(
    const LaunchItem& item, const QString& dataDir, bool forceAdmin, QString* error)
{
    auto fail = [error](const QString& msg) -> bool {
        if (error)
            *error = msg;
        return false;
    };
    const QString rawTarget = item.target.trimmed();
    if (rawTarget.isEmpty())
        return fail(QStringLiteral("启动目标为空"));

    ComInitGuard com;
    auto exec = [&](const QString& file, const QString& params, LPCWSTR verb) -> bool {
        const HINSTANCE h = ::ShellExecuteW(
            nullptr, verb, file.toStdWString().c_str(),
            params.isEmpty() ? nullptr : params.toStdWString().c_str(),
            nullptr, SW_SHOWNORMAL);
        const auto code = reinterpret_cast<INT_PTR>(h);
        if (code <= 32)
            return fail(QStringLiteral("启动失败 (ShellExecute 错误码 %1)：%2").arg(code).arg(file));
        return true;
    };

    if (isWebUrl(rawTarget))
        return exec(rawTarget, QString{}, L"open");

    const LPCWSTR verb = (forceAdmin || item.runAsAdmin) ? L"runas" : L"open";
    if (needsShimLnk(item, forceAdmin)) {
        const QString lnk = ensureLaunchLnk(dataDir, item, forceAdmin, error);
        if (lnk.isEmpty())
            return false;
        return exec(lnk, QString{}, verb);
    }

    const QString resolved = resolveFullPath(rawTarget);
    if (resolved.isEmpty() || !QFileInfo::exists(resolved))
        return fail(QStringLiteral("目标不存在：%1").arg(rawTarget));
    return exec(resolved, QString{}, verb);
}

bool ShellBridge::standaloneReveal(const LaunchItem& item, QString* error)
{
    auto fail = [error](const QString& msg) -> bool {
        if (error)
            *error = msg;
        return false;
    };
    const QString rawTarget = item.target.trimmed();
    if (rawTarget.isEmpty() || isWebUrl(rawTarget) || isSpecialSystemUri(rawTarget))
        return fail(QStringLiteral("当前条目不支持定位所在文件夹"));

    const QString resolved = resolveFullPath(rawTarget);
    const QFileInfo fi(resolved);
    if (!fi.exists())
        return fail(QStringLiteral("目标不存在：%1").arg(resolved));

    ComInitGuard com;
    const QString params = QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(fi.absoluteFilePath()));
    const HINSTANCE h = ::ShellExecuteW(
        nullptr, L"open", L"explorer.exe", params.toStdWString().c_str(), nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(h);
    if (code <= 32)
        return fail(QStringLiteral("打开资源管理器失败 (错误码 %1)").arg(code));
    return true;
}

} // namespace ql
