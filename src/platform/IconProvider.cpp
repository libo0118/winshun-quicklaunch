#include "platform/IconProvider.h"
#include "platform/ShellBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#include <windows.h>
#include <objbase.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

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
IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString decoded = QUrl::fromPercentEncoding(id.toUtf8()).trimmed();
    QImage base;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_cache.constFind(decoded);
        if (it != m_cache.constEnd())
            base = it.value();
    }

    if (base.isNull()) {
        base = extractIconImage(decoded);
        if (base.isNull())
            base = fallbackIcon();

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_cache.contains(decoded)) {
            if (static_cast<int>(m_cache.size()) >= kMaxCacheEntries && !m_order.empty()) {
                m_cache.remove(m_order.front());
                m_order.pop_front();
            }
            m_cache.insert(decoded, base);
            m_order.push_back(decoded);
        } else {
            base = m_cache.value(decoded);
        }
    }

    QImage out = base;
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && out.size() != requestedSize) {
        out = out.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = out.size();
    return out;
}

QImage IconProvider::extractIconImage(const QString& decodedId) const
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

QImage IconProvider::fallbackIcon() const
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
