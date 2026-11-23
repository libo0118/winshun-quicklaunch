#include "protocol/Protocol.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>

#include <windows.h>

#include <string>
#include <utility>

namespace ql {
namespace {

bool popNextLine(std::string& buffer, QByteArray* out)
{
    for (;;) {
        const std::size_t nl = buffer.find('\n');
        if (nl == std::string::npos)
            return false;
        QByteArray line = QByteArray(buffer.data(), static_cast<qsizetype>(nl)).trimmed();
        buffer.erase(0, nl + 1);
        if (!line.isEmpty()) {
            *out = std::move(line);
            return true;
        }
    }
}

void writeAllToStdout(const QByteArray& data)
{
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE)
        return;
    qsizetype offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(data.size() - offset);
        if (!::WriteFile(out, data.constData() + offset, chunk, &written, nullptr) || written == 0)
            return;
        offset += static_cast<qsizetype>(written);
    }
}

} // namespace

std::optional<QJsonObject> Protocol::readFirst(QByteArray* rest)
{
    const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    if (in == nullptr || in == INVALID_HANDLE_VALUE)
        return std::nullopt;
    std::string pending;
    char buf[65536];
    QByteArray line;
    while (!popNextLine(pending, &line)) {
        DWORD got = 0;
        if (!::ReadFile(in, buf, sizeof(buf), &got, nullptr) || got == 0)
            break;
        pending.append(buf, got);
    }
    if (line.isEmpty() && !pending.empty()) {
        line = QByteArray(pending.data(), static_cast<qsizetype>(pending.size())).trimmed();
        pending.clear();
    }
    if (rest)
        *rest = QByteArray(pending.data(), static_cast<qsizetype>(pending.size()));
    if (line.isEmpty())
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return std::nullopt;
    return doc.object();
}

Protocol::Protocol(QByteArray pending, QObject* parent)
    : QObject(parent)
    , m_pending(std::move(pending))
{
}

Protocol::~Protocol()
{
    m_stopping.store(true, std::memory_order_release);
    if (m_readerThread.joinable()) {
        const HANDLE hThread = static_cast<HANDLE>(m_readerThread.native_handle());
        for (int i = 0; i < 100; ++i) {
            if (hThread)
                ::CancelSynchronousIo(hThread);
            if (::WaitForSingleObject(hThread, 10) == WAIT_OBJECT_0)
                break;
        }
        m_readerThread.join();
    }
}

void Protocol::start()
{
    if (m_started.exchange(true, std::memory_order_acq_rel))
        return;
    QByteArray initial = std::move(m_pending);
    m_pending.clear();
    m_readerThread = std::thread([this, initial = std::move(initial)]() mutable {
        readLoop(std::move(initial));
    });
}

void Protocol::readLoop(QByteArray initialPending)
{
    const QPointer<Protocol> self(this);
    std::string buffered(initialPending.constData(), static_cast<std::size_t>(initialPending.size()));
    const HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    char buf[65536];

    auto dispatchLine = [&](QByteArray line) {
        QMetaObject::invokeMethod(this, [self, line = std::move(line)]() {
            if (self)
                self->received(line);
        }, Qt::QueuedConnection);
    };

    for (;;) {
        QByteArray line;
        while (popNextLine(buffered, &line))
            dispatchLine(std::move(line));
        if (m_stopping.load(std::memory_order_acquire) || in == nullptr || in == INVALID_HANDLE_VALUE)
            break;
        DWORD got = 0;
        if (!::ReadFile(in, buf, sizeof(buf), &got, nullptr) || got == 0) {
            if (!m_stopping.load(std::memory_order_acquire) && !buffered.empty()) {
                QByteArray tail = QByteArray(buffered.data(), static_cast<qsizetype>(buffered.size())).trimmed();
                if (!tail.isEmpty())
                    dispatchLine(std::move(tail));
            }
            break;
        }
        buffered.append(buf, got);
    }

    if (!m_stopping.load(std::memory_order_acquire)) {
        QMetaObject::invokeMethod(this, [self]() {
            if (self)
                emit self->closed();
        }, Qt::QueuedConnection);
    }
}

void Protocol::received(const QByteArray& line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (!doc.isObject()) {
        qWarning().noquote() << "Protocol: invalid JSON object:" << err.errorString();
        return;
    }
    const QJsonObject msg = doc.object();
    const QJsonValue id = msg.value(QStringLiteral("id"));
    const QString method = msg.value(QStringLiteral("method")).toString();
    if (method.isEmpty()) {
        const auto it = m_replies.find(id.toString());
        if (it == m_replies.end())
            return;
        Reply reply = std::move(it->second);
        m_replies.erase(it);
        if (!reply)
            return;
        const QJsonObject failure = msg.value(QStringLiteral("error")).toObject();
        if (!failure.isEmpty()) {
            const int code = failure.value(QStringLiteral("code")).toInt();
            const QString message = failure.value(QStringLiteral("message")).toString(
                QStringLiteral("error %1").arg(code));
            reply(QJsonValue{}, message);
        } else {
            reply(msg.value(QStringLiteral("result")), QString{});
        }
        return;
    }
    const QJsonObject params = msg.value(QStringLiteral("params")).toObject();
    if (id.isUndefined() || id.isNull())
        emit notified(method, params);
    else
        emit requested(id, method, params);
}

void Protocol::write(const QJsonObject& message)
{
    QJsonObject full = message;
    full.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    QByteArray line = QJsonDocument(full).toJson(QJsonDocument::Compact);
    line.append('\n');
    std::lock_guard<std::mutex> lock(m_writeMutex);
    writeAllToStdout(line);
}

void Protocol::respond(const QJsonValue& id, const QJsonValue& result)
{
    write(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void Protocol::respondError(const QJsonValue& id, int code, const QString& message)
{
    const QJsonObject err{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    write(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("error"), err}});
}

void Protocol::notify(const QString& method, const QJsonObject& params)
{
    write(QJsonObject{{QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void Protocol::request(const QString& method, const QJsonObject& params, Reply reply)
{
    const QString id = QStringLiteral("ql-%1").arg(m_nextId++);
    if (reply)
        m_replies[id] = std::move(reply);
    write(QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params}
    });
}

void Protocol::tool(const QString& name, const QJsonObject& arguments, Reply reply)
{
    const QJsonObject params{
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), arguments}
    };
    request(QStringLiteral("winshun/tool"), params,
        [name, reply = std::move(reply)](const QJsonValue& result, const QString& error) {
            if (!error.isEmpty())
                qWarning().noquote() << "Protocol tool" << name << "failed:" << error;
            if (reply)
                reply(result, error);
        });
}

void Protocol::sendHotkeysReserved(const QStringList& keys)
{
    notify(QStringLiteral("winshun/hotkeys"),
        QJsonObject{{QStringLiteral("keys"), QJsonArray::fromStringList(keys)}});
}

void Protocol::sendSettingsUpdate(const QJsonObject& settings)
{
    notify(QStringLiteral("winshun/settings/update"),
        QJsonObject{{QStringLiteral("settings"), settings}});
}

void Protocol::log(const QString& level, const QString& text)
{
    notify(QStringLiteral("winshun/log"),
        QJsonObject{{QStringLiteral("level"), level}, {QStringLiteral("text"), text}});
}

void Protocol::notifyUser(const QString& text)
{
    notify(QStringLiteral("winshun/notify"),
        QJsonObject{{QStringLiteral("text"), text}});
}

} // namespace ql
