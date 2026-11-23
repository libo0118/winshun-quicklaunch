#include "platform/HotkeyService.h"
#include "protocol/Protocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#include <windows.h>

namespace ql {
namespace {

struct ParsedHotkey {
    QString normalized;
    UINT modifiers = 0;
    UINT vk = 0;
    bool allowBare = false;
};

bool parseHotkeyImpl(const QString& raw, ParsedHotkey* out)
{
    bool hasWin = false;
    bool hasCtrl = false;
    bool hasAlt = false;
    bool hasShift = false;
    QString mainName;
    UINT vk = 0;
    bool allowBare = false;

    const QStringList parts = raw.split(u'+', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString token = part.trimmed().toLower();
        if (token.isEmpty())
            return false;
        if (token == QLatin1String("ctrl") || token == QLatin1String("control")) {
            hasCtrl = true;
        } else if (token == QLatin1String("alt")) {
            hasAlt = true;
        } else if (token == QLatin1String("shift")) {
            hasShift = true;
        } else if (token == QLatin1String("win") || token == QLatin1String("meta") || token == QLatin1String("super")) {
            hasWin = true;
        } else {
            if (!mainName.isEmpty())
                return false;
            if (token.size() == 1 && token[0].unicode() < 128 && token[0].isLetterOrNumber()) {
                const QChar upper = token[0].toUpper();
                mainName = QString(upper);
                vk = upper.unicode();
            } else if (token.startsWith(u'f') && token.size() <= 3) {
                bool ok = false;
                const int fn = token.mid(1).toInt(&ok);
                if (!ok || fn < 1 || fn > 24)
                    return false;
                mainName = QStringLiteral("F%1").arg(fn);
                vk = static_cast<UINT>(VK_F1 + fn - 1);
                allowBare = true;
            } else if (token == QLatin1String("space")) {
                mainName = QStringLiteral("Space");
                vk = VK_SPACE;
            } else if (token == QLatin1String("tab")) {
                mainName = QStringLiteral("Tab");
                vk = VK_TAB;
            } else if (token == QLatin1String("enter") || token == QLatin1String("return")) {
                mainName = QStringLiteral("Enter");
                vk = VK_RETURN;
            } else if (token == QLatin1String("esc") || token == QLatin1String("escape")) {
                mainName = QStringLiteral("Esc");
                vk = VK_ESCAPE;
            } else if (token == QLatin1String("back") || token == QLatin1String("backspace")) {
                mainName = QStringLiteral("Back");
                vk = VK_BACK;
            } else if (token == QLatin1String("insert") || token == QLatin1String("ins")) {
                mainName = QStringLiteral("Insert");
                vk = VK_INSERT;
            } else if (token == QLatin1String("delete") || token == QLatin1String("del")) {
                mainName = QStringLiteral("Delete");
                vk = VK_DELETE;
            } else if (token == QLatin1String("home")) {
                mainName = QStringLiteral("Home");
                vk = VK_HOME;
            } else if (token == QLatin1String("end")) {
                mainName = QStringLiteral("End");
                vk = VK_END;
            } else if (token == QLatin1String("pageup") || token == QLatin1String("pgup") || token == QLatin1String("prior")) {
                mainName = QStringLiteral("PageUp");
                vk = VK_PRIOR;
            } else if (token == QLatin1String("pagedown") || token == QLatin1String("pgdown") || token == QLatin1String("next")) {
                mainName = QStringLiteral("PageDown");
                vk = VK_NEXT;
            } else if (token == QLatin1String("up")) {
                mainName = QStringLiteral("Up");
                vk = VK_UP;
            } else if (token == QLatin1String("down")) {
                mainName = QStringLiteral("Down");
                vk = VK_DOWN;
            } else if (token == QLatin1String("left")) {
                mainName = QStringLiteral("Left");
                vk = VK_LEFT;
            } else if (token == QLatin1String("right")) {
                mainName = QStringLiteral("Right");
                vk = VK_RIGHT;
            } else if (token == QLatin1String("`") || token == QLatin1String("backquote")) {
                mainName = QStringLiteral("`");
                vk = VK_OEM_3;
            } else if (token == QLatin1String("printscreen") || token == QLatin1String("print") || token == QLatin1String("prtsc")) {
                mainName = QStringLiteral("PrintScreen");
                vk = VK_SNAPSHOT;
                allowBare = true;
            } else {
                return false;
            }
        }
    }

    if (mainName.isEmpty() || vk == 0)
        return false;

    if (out) {
        UINT mods = 0;
        QStringList formatted;
        if (hasWin) {
            mods |= MOD_WIN;
            formatted.append(QStringLiteral("Win"));
        }
        if (hasCtrl) {
            mods |= MOD_CONTROL;
            formatted.append(QStringLiteral("Ctrl"));
        }
        if (hasAlt) {
            mods |= MOD_ALT;
            formatted.append(QStringLiteral("Alt"));
        }
        if (hasShift) {
            mods |= MOD_SHIFT;
            formatted.append(QStringLiteral("Shift"));
        }
        formatted.append(mainName);
        out->normalized = formatted.join(u'+');
        out->modifiers = mods;
        out->vk = vk;
        out->allowBare = allowBare;
    }
    return true;
}

QString formatHotkeyError(const QString& key, int code)
{
    if (code == ERROR_HOTKEY_ALREADY_REGISTERED)
        return QStringLiteral("快捷键 %1 已被其他程序占用 (1409)").arg(key);
    if (code == ERROR_INVALID_PARAMETER)
        return QStringLiteral("不支持该键位组合：%1 (87)").arg(key);
    return QStringLiteral("注册快捷键 %1 失败 (错误码 %2)").arg(key).arg(code);
}

} // namespace

HotkeyService::HotkeyService(Protocol* protocol, bool hostSupportsHotkeys, QObject* parent)
    : QObject(parent)
    , m_protocol(protocol)
    , m_hostSupportsHotkeys(hostSupportsHotkeys)
{
    if (auto* app = QCoreApplication::instance())
        app->installNativeEventFilter(this);
}

HotkeyService::~HotkeyService()
{
    if (!m_activeNormalized.isEmpty() || m_localRegistered) {
        releaseInternal(m_protocol != nullptr, false);
    }
    if (auto* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

void HotkeyService::setHostSupportsHotkeys(bool supported)
{
    m_hostSupportsHotkeys = supported;
}

void HotkeyService::applyHotkey(const QString& key)
{
    const quint64 rev = ++m_revision;
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        releaseInternal(true, false);
        m_status = HotkeyStatus{};
        emit statusChanged(m_status);
        return;
    }

    ParsedHotkey parsed;
    if (!parseHotkeyImpl(trimmed, &parsed)) {
        releaseInternal(true, false);
        m_status = HotkeyStatus{
            trimmed, false, m_protocol && m_hostSupportsHotkeys,
            ERROR_INVALID_PARAMETER, formatHotkeyError(trimmed, ERROR_INVALID_PARAMETER)
        };
        emit statusChanged(m_status);
        return;
    }

    if (m_localRegistered) {
        ::UnregisterHotKey(nullptr, kHotkeyId);
        m_localRegistered = false;
    }

    if (m_protocol && m_hostSupportsHotkeys) {
        m_activeNormalized = parsed.normalized;
        m_status = HotkeyStatus{parsed.normalized, false, true, 0, QString{}};
        m_protocol->sendHotkeysReserved(QStringList{parsed.normalized});
        const QJsonObject args{{QStringLiteral("keys"), QJsonArray{parsed.normalized}}};
        m_protocol->tool(QStringLiteral("register_hotkeys"), args,
            [self = QPointer<HotkeyService>(this), norm = parsed.normalized, rev](
                const QJsonValue& result, const QString& error) {
                if (!self || rev != self->m_revision)
                    return;
                HotkeyStatus next;
                next.key = norm;
                next.viaHost = true;
                if (!error.isEmpty()) {
                    next.registered = false;
                    next.errorCode = -1;
                    next.errorMessage = error;
                } else {
                    const QJsonObject errs = result.toObject().value(QStringLiteral("errors")).toObject();
                    if (errs.contains(norm)) {
                        next.registered = false;
                        next.errorCode = errs.value(norm).toInt();
                        next.errorMessage = formatHotkeyError(norm, next.errorCode);
                    } else {
                        next.registered = true;
                    }
                }
                self->m_status = next;
                emit self->statusChanged(self->m_status);
            });
        return;
    }
    if (!parsed.allowBare && parsed.modifiers == 0) {
        if (m_protocol)
            m_protocol->sendHotkeysReserved(QStringList{});
        m_activeNormalized.clear();
        m_status = HotkeyStatus{
            parsed.normalized, false, false,
            ERROR_INVALID_PARAMETER, formatHotkeyError(parsed.normalized, ERROR_INVALID_PARAMETER)
        };
        emit statusChanged(m_status);
        return;
    }

    if (m_protocol)
        m_protocol->sendHotkeysReserved(QStringList{parsed.normalized});
    attemptLocalRegister(parsed.normalized, parsed.modifiers, parsed.vk, true, rev);
}

void HotkeyService::attemptLocalRegister(
    const QString& normalized, unsigned int mods, unsigned int vk, bool allowRetry, quint64 revision)
{
    if (::RegisterHotKey(nullptr, kHotkeyId, mods | MOD_NOREPEAT, vk)) {
        m_localRegistered = true;
        m_activeNormalized = normalized;
        m_status = HotkeyStatus{normalized, true, false, 0, QString{}};
        emit statusChanged(m_status);
        return;
    }
    const DWORD err = ::GetLastError();
    m_localRegistered = false;
    m_activeNormalized = normalized;
    m_status = HotkeyStatus{normalized, false, false, static_cast<int>(err), formatHotkeyError(normalized, static_cast<int>(err))};
    if (allowRetry && err == ERROR_HOTKEY_ALREADY_REGISTERED) {
        QTimer::singleShot(300, this, [self = QPointer<HotkeyService>(this), normalized, mods, vk, revision]() {
            if (!self || revision != self->m_revision)
                return;
            self->attemptLocalRegister(normalized, mods, vk, false, revision);
        });
        return;
    }
    emit statusChanged(m_status);
}

void HotkeyService::handleHostHotkey(const QString& key)
{
    const QString norm = normalizeHotkey(key);
    if (!norm.isEmpty() && norm == m_activeNormalized)
        emit triggered();
}

void HotkeyService::releaseAll()
{
    ++m_revision;
    releaseInternal(true, true);
}

void HotkeyService::releaseInternal(bool notifyHost, bool emitStatus)
{
    m_activeNormalized.clear();
    if (m_localRegistered) {
        ::UnregisterHotKey(nullptr, kHotkeyId);
        m_localRegistered = false;
    }
    if (notifyHost && m_protocol) {
        m_protocol->sendHotkeysReserved(QStringList{});
        if (m_hostSupportsHotkeys)
            m_protocol->tool(QStringLiteral("register_hotkeys"), {{QStringLiteral("keys"), QJsonArray{}}});
    }
    if (emitStatus) {
        m_status = HotkeyStatus{};
        emit statusChanged(m_status);
    }
}

HotkeyStatus HotkeyService::currentStatus() const
{
    return m_status;
}

QString HotkeyService::normalizeHotkey(const QString& key)
{
    ParsedHotkey parsed;
    return parseHotkeyImpl(key, &parsed) ? parsed.normalized : QString{};
}

bool HotkeyService::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (!message || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG"))
        return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == static_cast<WPARAM>(kHotkeyId)) {
        emit triggered();
        if (result)
            *result = 0;
        return true;
    }
    return false;
}

} // namespace ql