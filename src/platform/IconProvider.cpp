#include "platform/IconProvider.h"
#include "platform/ShellBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QThreadPool>
#include <QUrl>

#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace ql {
namespace {

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

struct IconGuard {
    HICON h = nullptr;
    ~IconGuard()
    {
        if (h)
            ::DestroyIcon(h);
    }
};

struct BitmapGuard {
    HBITMAP h = nullptr;
    ~BitmapGuard()
    {
        if (h)
            ::DeleteObject(h);
    }
};

struct DcGuard {
    HDC h = nullptr;
    ~DcGuard()
    {
        if (h)
            ::DeleteDC(h);
    }
};

template <typename T>
struct ComRelease {
    T* ptr = nullptr;
    ~ComRelease()
    {
        if (ptr)
            ptr->Release();
    }
};

QString systemDllPath(const wchar_t* dllName)
{
    wchar_t sysDir[MAX_PATH]{};
    const UINT len = ::GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return QString::fromWCharArray(dllName);
    return QDir(QString::fromWCharArray(sysDir, static_cast<int>(len)))
        .filePath(QString::fromWCharArray(dllName));
}
QImage hiconToImage(HICON hIcon)
{
    if (!hIcon)
        return {};
    ICONINFO ii{};
    if (!::GetIconInfo(hIcon, &ii))
        return {};
    BitmapGuard colorBm{ii.hbmColor};
    BitmapGuard maskBm{ii.hbmMask};

    BITMAP bm{};
    if (::GetObjectW(colorBm.h ? colorBm.h : maskBm.h, sizeof(bm), &bm) == 0)
        return {};
    const int w = bm.bmWidth;
    const int h = colorBm.h ? bm.bmHeight : (bm.bmHeight / 2);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
        return {};

    DcGuard dc{::CreateCompatibleDC(nullptr)};
    if (!dc.h)
        return {};

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (colorBm.h) {
        QImage img(w, h, QImage::Format_ARGB32);
        if (::GetDIBits(dc.h, colorBm.h, 0, static_cast<UINT>(h), img.bits(), &bmi, DIB_RGB_COLORS) == 0)
            return {};
        auto* px = reinterpret_cast<quint32*>(img.bits());
        const qsizetype count = static_cast<qsizetype>(w) * h;
        bool hasAlpha = false;
        for (qsizetype i = 0; i < count; ++i) {
            if ((px[i] & 0xFF000000u) != 0) {
                hasAlpha = true;
                break;
            }
        }
        if (!hasAlpha && maskBm.h) {
            std::vector<quint32> mask(static_cast<std::size_t>(count), 0);
            if (::GetDIBits(dc.h, maskBm.h, 0, static_cast<UINT>(h), mask.data(), &bmi, DIB_RGB_COLORS) != 0) {
                for (qsizetype i = 0; i < count; ++i)
                    px[i] = ((mask[i] & 0x00FFFFFFu) == 0) ? (px[i] | 0xFF000000u) : 0u;
            }
        }
        return img;
    }

    if (!maskBm.h)
        return {};
    std::vector<quint32> mono(static_cast<std::size_t>(w) * h * 2, 0);
    bmi.bmiHeader.biHeight = -(h * 2);
    if (::GetDIBits(dc.h, maskBm.h, 0, static_cast<UINT>(h * 2), mono.data(), &bmi, DIB_RGB_COLORS) == 0)
        return {};
    QImage img(w, h, QImage::Format_ARGB32);
    auto* px = reinterpret_cast<quint32*>(img.bits());
    for (int i = 0; i < w * h; ++i) {
        const bool andZero = (mono[static_cast<std::size_t>(i)] & 0x00FFFFFFu) == 0;
        const bool xorZero = (mono[static_cast<std::size_t>(i + w * h)] & 0x00FFFFFFu) == 0;
        if (andZero)
            px[i] = xorZero ? 0xFF000000u : 0xFFFFFFFFu;
        else
            px[i] = xorZero ? 0x00000000u : 0xFF202020u;
    }
    return img;
}
QImage extractByIndex(const QString& filePath, int index)
{
    IconGuard largeIcon;
    const auto wPath = QDir::toNativeSeparators(filePath).toStdWString();
    if (::ExtractIconExW(wPath.c_str(), index, &largeIcon.h, nullptr, 1) > 0 && largeIcon.h)
        return hiconToImage(largeIcon.h);
    return {};
}

QImage extractViaShell(const QString& pathOrExt, bool exists)
{
    const auto wTarget = pathOrExt.toStdWString();
    const DWORD attrs = exists ? 0u : FILE_ATTRIBUTE_NORMAL;
    const UINT baseFlag = exists ? 0u : SHGFI_USEFILEATTRIBUTES;

    SHFILEINFOW sfi{};
    if (::SHGetFileInfoW(wTarget.c_str(), attrs, &sfi, sizeof(sfi), baseFlag | SHGFI_SYSICONINDEX) != 0) {
        ComRelease<IImageList> iml;
        if (SUCCEEDED(::SHGetImageList(SHIL_EXTRALARGE, IID_PPV_ARGS(&iml.ptr))) && iml.ptr) {
            IconGuard icon;
            if (SUCCEEDED(iml.ptr->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon.h)) && icon.h) {
                QImage img = hiconToImage(icon.h);
                if (!img.isNull())
                    return img;
            }
        }
    }

    SHFILEINFOW sfiIcon{};
    if (::SHGetFileInfoW(wTarget.c_str(), attrs, &sfiIcon, sizeof(sfiIcon),
            baseFlag | SHGFI_ICON | SHGFI_LARGEICON) != 0) {
        IconGuard icon{sfiIcon.hIcon};
        if (icon.h)
            return hiconToImage(icon.h);
    }
    return {};
}

QImage loadDirectImageFile(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext != QLatin1String("png") && ext != QLatin1String("jpg")
        && ext != QLatin1String("jpeg") && ext != QLatin1String("ico")) {
        return {};
    }
    QImage img(path);
    if (ext == QLatin1String("ico") && (img.isNull() || img.width() < 32)) {
        IconGuard ico{static_cast<HICON>(::LoadImageW(
            nullptr, QDir::toNativeSeparators(path).toStdWString().c_str(),
            IMAGE_ICON, 64, 64, LR_LOADFROMFILE))};
        if (ico.h) {
            QImage hImg = hiconToImage(ico.h);
            if (!hImg.isNull())
                return hImg;
        }
    }
    return img.isNull() ? QImage{} : img.convertToFormat(QImage::Format_ARGB32);
}

} // namespace

struct IconCacheState {
    static constexpr int kMaxCacheEntries = 512;
    static constexpr int kMaxPrewarmBatch = 120;

    std::atomic<bool> alive{true};
    std::mutex mutex;
    QHash<QString, QImage> cache;
    std::deque<QString> order;

    static QImage extractIconImage(const QString& decodedId);
    static QImage fallbackIcon();

    QImage insertLocked(const QString& key, const QImage& img)
    {
        if (const auto it = cache.constFind(key); it != cache.constEnd())
            return it.value();
        if (static_cast<int>(cache.size()) >= kMaxCacheEntries && !order.empty()) {
            cache.remove(order.front());
            order.pop_front();
        }
        cache.insert(key, img);
        order.push_back(key);
        return img;
    }
};

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_state(std::make_shared<IconCacheState>())
{
}

IconProvider::~IconProvider()
{
    if (m_state)
        m_state->alive.store(false, std::memory_order_release);
}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString decoded = QUrl::fromPercentEncoding(id.toUtf8()).trimmed();
    QImage base;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        const auto it = m_state->cache.constFind(decoded);
        if (it != m_state->cache.constEnd())
            base = it.value();
    }

    if (base.isNull()) {
        base = IconCacheState::extractIconImage(decoded);
        if (base.isNull())
            base = IconCacheState::fallbackIcon();

        std::lock_guard<std::mutex> lock(m_state->mutex);
        base = m_state->insertLocked(decoded, base);
    }

    QImage out = base;
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && (out.width() > requestedSize.width() || out.height() > requestedSize.height())) {
        out = out.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = out.size();
    return out;
}

void IconProvider::prewarmAsync(const QStringList& iconKeys)
{
    if (!m_state || !m_state->alive.load(std::memory_order_acquire) || iconKeys.isEmpty())
        return;

    QStringList candidates;
    QSet<QString> seen;
    candidates.reserve(std::min<qsizetype>(iconKeys.size(), IconCacheState::kMaxPrewarmBatch));
    for (const QString& rawKey : iconKeys) {
        const QString key = rawKey.trimmed();
        if (key.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        candidates.append(key);
        if (candidates.size() >= IconCacheState::kMaxPrewarmBatch)
            break;
    }
    if (candidates.isEmpty())
        return;

    QStringList keysToLoad;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (!m_state->alive.load(std::memory_order_acquire))
            return;
        keysToLoad.reserve(candidates.size());
        for (const QString& key : candidates) {
            if (!m_state->cache.contains(key))
                keysToLoad.append(key);
        }
    }
    if (keysToLoad.isEmpty())
        return;

    QThreadPool::globalInstance()->start(
        [weakState = std::weak_ptr<IconCacheState>(m_state),
         keys = std::move(keysToLoad)]() {
            for (const QString& key : keys) {
                {
                    const auto state = weakState.lock();
                    if (!state || !state->alive.load(std::memory_order_acquire))
                        return;
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (!state->alive.load(std::memory_order_acquire))
                        return;
                    if (state->cache.contains(key))
                        continue;
                }
                QImage img = IconCacheState::extractIconImage(key);
                if (img.isNull())
                    img = IconCacheState::fallbackIcon();
                {
                    const auto state = weakState.lock();
                    if (!state || !state->alive.load(std::memory_order_acquire))
                        return;
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (!state->alive.load(std::memory_order_acquire))
                        return;
                    state->insertLocked(key, img);
                }
            }
        });
}

QImage IconCacheState::extractIconImage(const QString& decodedId)
{
    if (decodedId.isEmpty())
        return fallbackIcon();

    ComInitGuard com;
    QString target = decodedId;
    if (target.startsWith(QLatin1String("custom:"), Qt::CaseInsensitive)) {
        target = ShellBridge::expandPath(target.mid(7).trimmed());
        if (const QImage direct = loadDirectImageFile(target); !direct.isNull())
            return direct;
    } else {
        const QString expanded = ShellBridge::expandPath(target);
        if (const QImage direct = loadDirectImageFile(expanded); !direct.isNull())
            return direct;
    }

    if (target.compare(QLatin1String("$computer"), Qt::CaseInsensitive) == 0) {
        if (const QImage img = extractByIndex(systemDllPath(L"shell32.dll"), 15); !img.isNull())
            return img;
    }
    if (target.compare(QLatin1String("$controls"), Qt::CaseInsensitive) == 0) {
        if (const QImage img = extractByIndex(systemDllPath(L"shell32.dll"), 21); !img.isNull())
            return img;
        if (const QImage img = extractByIndex(systemDllPath(L"imageres.dll"), 27); !img.isNull())
            return img;
    }
    if (ShellBridge::isWebUrl(target)) {
        if (const QImage img = extractViaShell(QStringLiteral(".html"), false); !img.isNull())
            return img;
    }

    const qsizetype comma = target.lastIndexOf(u',');
    if (comma > 0) {
        bool ok = false;
        const int idx = target.mid(comma + 1).trimmed().toInt(&ok);
        if (ok) {
            const QString iconFile = ShellBridge::resolveFullPath(target.left(comma).trimmed());
            if (!iconFile.isEmpty() && QFileInfo::exists(iconFile)) {
                if (const QImage img = extractByIndex(iconFile, idx); !img.isNull())
                    return img;
            }
        }
    }

    const QString resolved = ShellBridge::resolveFullPath(target);
    const QString candidate = resolved.isEmpty() ? target : resolved;
    const bool exists = !candidate.isEmpty() && QFileInfo::exists(candidate);
    if (const QImage img = extractViaShell(candidate, exists); !img.isNull())
        return img;

    return fallbackIcon();
}

QImage IconCacheState::fallbackIcon()
{
    QImage img(48, 48, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath bg;
    bg.addRoundedRect(QRectF(4, 4, 40, 40), 10, 10);
    p.fillPath(bg, QColor(0x33, 0x78, 0xF6));
    p.setPen(QPen(QColor(0xFF, 0xFF, 0xFF, 50), 1.0));
    p.drawPath(bg);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, 235));
    p.drawRoundedRect(QRectF(14, 14, 8, 8), 2, 2);
    p.drawRoundedRect(QRectF(26, 14, 8, 8), 2, 2);
    p.drawRoundedRect(QRectF(14, 26, 8, 8), 2, 2);
    p.drawRoundedRect(QRectF(26, 26, 8, 8), 2, 2);
    return img;
}

} // namespace ql
