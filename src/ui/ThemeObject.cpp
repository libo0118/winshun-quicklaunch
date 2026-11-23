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
    copy.setAlphaF(std::clamp(static_cast<double>(alpha), 0.0, 1.0));
    return copy;
}

} // namespace ql
