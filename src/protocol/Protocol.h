#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace ql {

class Protocol : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonValue& result, const QString& error)>;

    static std::optional<QJsonObject> readFirst(QByteArray* rest);

    explicit Protocol(QByteArray pending, QObject* parent = nullptr);
    ~Protocol() override;

    void start();
    void respond(const QJsonValue& id, const QJsonValue& result);
    void respondError(const QJsonValue& id, int code, const QString& message);
    void notify(const QString& method, const QJsonObject& params = {});
    void request(const QString& method, const QJsonObject& params, Reply reply = {});
    void tool(const QString& name, const QJsonObject& arguments, Reply reply = {});
    void sendHotkeysReserved(const QStringList& keys);
    void sendSettingsUpdate(const QJsonObject& settings);
    void log(const QString& level, const QString& text);
    void notifyUser(const QString& text);

signals:
    void requested(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void notified(const QString& method, const QJsonObject& params);
    void closed();

private:
    void readLoop(QByteArray initialPending);
    void received(const QByteArray& line);
    void write(const QJsonObject& message);

    QByteArray m_pending;
    std::map<QString, Reply> m_replies;
    quint64 m_nextId = 1;
    std::mutex m_writeMutex;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopping{false};
    std::thread m_readerThread;
};

} // namespace ql
