#pragma once

#include "core/Models.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>

namespace ql {

class Protocol;

class HotkeyService : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit HotkeyService(Protocol* protocol, bool hostSupportsHotkeys, QObject* parent = nullptr);
    ~HotkeyService() override;

    void setHostSupportsHotkeys(bool supported);
    void applyHotkey(const QString& key);
    void handleHostHotkey(const QString& key);
    void releaseAll();
    HotkeyStatus currentStatus() const;

    static QString normalizeHotkey(const QString& key);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void triggered();
    void statusChanged(const ql::HotkeyStatus& status);

private:
    void attemptLocalRegister(const QString& normalized, unsigned int mods, unsigned int vk, bool allowRetry, quint64 revision);
    void releaseInternal(bool notifyHost, bool emitStatus);

    static constexpr int kHotkeyId = 0x514C;

    QPointer<Protocol> m_protocol;
    bool m_hostSupportsHotkeys = false;
    bool m_localRegistered = false;
    quint64 m_revision = 0;
    QString m_activeNormalized;
    HotkeyStatus m_status;
};

} // namespace ql
