#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

#include <memory>

namespace ql {

struct IconCacheState;

class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider();
    ~IconProvider() override;

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    void prewarmAsync(const QStringList& iconKeys);

private:
    std::shared_ptr<IconCacheState> m_state;
};

} // namespace ql
