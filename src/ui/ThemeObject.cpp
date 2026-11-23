#include "ui/ThemeObject.h"

#include <algorithm>
#include <tuple>

namespace ql {

namespace {

auto themeTuple(const ThemeData& d) {
    return std::tie(
        d.dark, d.rounded, d.rememberWindowSize, d.locale,
        d.cardRadius, d.controlRadius,
        d.background, d.layer, d.page, d.windowEdge, d.divider,
        d.text, d.subtext, d.faint, d.hover, d.pressed,
        d.control, d.controlHover, d.controlBorder, d.inputFocused,
        d.menuBackground, d.menuHover,
        d.accent, d.accentFill, d.accentText, d.danger);
}

QColor opaquePageColor(const ThemeData& d)
{
    return QColor(blendOverOpaque(d.page, defaultPageBaseHex(d.dark)));
}

QColor opaqueLayerColor(const ThemeData& d)
{
    return QColor(blendOverOpaque(d.layer, opaquePageColor(d).name(QColor::HexArgb)));
}

} // namespace

ThemeObject::ThemeObject(QObject* parent)
    : QObject(parent)
{
}

void ThemeObject::update(const ThemeData& data)
{
    if (themeTuple(m_data) == themeTuple(data)) {
        return;
    }
    m_data = data;
    emit changed();
}

QColor ThemeObject::withAlpha(const QColor& color, qreal alpha) const
{
    QColor copy = color;
    const double factor = std::clamp(static_cast<double>(alpha), 0.0, 1.0);
    copy.setAlphaF(std::clamp(copy.alphaF() * factor, 0.0, 1.0));
    return copy;
}

QColor ThemeObject::surface() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFFFFF"));
    }
    // Lift composited dark layer slightly with white so floating dialogs stay distinct and opaque.
    return blendColors(QColor(QStringLiteral("#FFFFFFFF")), opaqueLayerColor(m_data), 0.06);
}

QColor ThemeObject::sidebarBackground() const
{
    const QColor pageColor = opaquePageColor(m_data);
    if (!m_data.dark) {
        return blendColors(QColor(QStringLiteral("#FFEEF1F6")), pageColor, 0.80);
    }
    return blendColors(QColor(QStringLiteral("#FF181A1F")), pageColor, 0.75);
}

QColor ThemeObject::cardSurface() const
{
    if (!m_data.dark) {
        return QColor(QStringLiteral("#FFF4F5F8"));
    }
    return blendColors(QColor(QStringLiteral("#FF2C2F37")), opaqueLayerColor(m_data), 0.70);
}

QColor ThemeObject::softBorder() const
{
    const QColor pageColor = opaquePageColor(m_data);
    if (!m_data.dark) {
        return blendColors(QColor(QStringLiteral("#FF0F172A")), pageColor, 0.08);
    }
    return blendColors(QColor(QStringLiteral("#FFFFFFFF")), pageColor, 0.09);
}

} // namespace ql
