#pragma once

#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <deque>
#include <mutex>

namespace ql {

class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QImage extractIconImage(const QString& decodedId) const;
    QImage fallbackIcon() const;

    static constexpr int kMaxCacheEntries = 512;

    mutable std::mutex m_mutex;
    QHash<QString, QImage> m_cache;
    std::deque<QString> m_order;
};

} // namespace ql
