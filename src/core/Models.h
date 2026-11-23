#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace ql {

struct CategoryEntry {
    QString id;
    QString name;
    int order = 0;

    QJsonObject toJson() const {
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("order"), order}
        };
    }

    static CategoryEntry fromJson(const QJsonObject& obj) {
        CategoryEntry c;
        c.id = obj.value(QStringLiteral("id")).toString();
        c.name = obj.value(QStringLiteral("name")).toString();
        c.order = obj.value(QStringLiteral("order")).toInt(0);
        return c;
    }
};

struct LaunchItem {
    QString id;
    QString categoryId;
    QString name;
    QString target;
    QString arguments;
    QString workingDir;
    QString iconPath;
    QString keywords;
    QString remark;
    bool runAsAdmin = false;
    int order = 0;
    int runCount = 0;
    qint64 lastRunUnix = 0;

    QJsonObject toJson() const {
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("categoryId"), categoryId},
            {QStringLiteral("name"), name},
            {QStringLiteral("target"), target},
            {QStringLiteral("arguments"), arguments},
            {QStringLiteral("workingDir"), workingDir},
            {QStringLiteral("iconPath"), iconPath},
            {QStringLiteral("keywords"), keywords},
            {QStringLiteral("remark"), remark},
            {QStringLiteral("runAsAdmin"), runAsAdmin},
            {QStringLiteral("order"), order},
            {QStringLiteral("runCount"), runCount},
            {QStringLiteral("lastRunUnix"), lastRunUnix}
        };
    }

    static LaunchItem fromJson(const QJsonObject& obj) {
        LaunchItem it;
        it.id = obj.value(QStringLiteral("id")).toString();
        it.categoryId = obj.value(QStringLiteral("categoryId")).toString();
        it.name = obj.value(QStringLiteral("name")).toString();
        it.target = obj.value(QStringLiteral("target")).toString();
        it.arguments = obj.value(QStringLiteral("arguments")).toString();
        it.workingDir = obj.value(QStringLiteral("workingDir")).toString();
        it.iconPath = obj.value(QStringLiteral("iconPath")).toString();
        it.keywords = obj.value(QStringLiteral("keywords")).toString();
        it.remark = obj.value(QStringLiteral("remark")).toString();
        it.runAsAdmin = obj.value(QStringLiteral("runAsAdmin")).toBool(false);
        it.order = obj.value(QStringLiteral("order")).toInt(0);
        it.runCount = obj.value(QStringLiteral("runCount")).toInt(0);
        it.lastRunUnix = obj.value(QStringLiteral("lastRunUnix")).toInteger(0);
        return it;
    }
};
struct LauncherDatabase {
    int version = 1;
    QString activeCategoryId;
    double windowWidth = 760.0;
    double windowHeight = 520.0;
    QList<CategoryEntry> categories;
    QList<LaunchItem> items;

    QJsonObject toJson() const {
        QJsonArray cats;
        for (const auto& c : categories) cats.append(c.toJson());
        QJsonArray its;
        for (const auto& it : items) its.append(it.toJson());
        return QJsonObject{
            {QStringLiteral("version"), version},
            {QStringLiteral("activeCategoryId"), activeCategoryId},
            {QStringLiteral("windowWidth"), windowWidth},
            {QStringLiteral("windowHeight"), windowHeight},
            {QStringLiteral("categories"), cats},
            {QStringLiteral("items"), its}
        };
    }

    static LauncherDatabase fromJson(const QJsonObject& obj) {
        LauncherDatabase db;
        db.version = obj.value(QStringLiteral("version")).toInt(1);
        db.activeCategoryId = obj.value(QStringLiteral("activeCategoryId")).toString();
        db.windowWidth = obj.value(QStringLiteral("windowWidth")).toDouble(760.0);
        db.windowHeight = obj.value(QStringLiteral("windowHeight")).toDouble(520.0);
        const auto cats = obj.value(QStringLiteral("categories")).toArray();
        for (const auto& v : cats) {
            if (v.isObject()) db.categories.append(CategoryEntry::fromJson(v.toObject()));
        }
        const auto its = obj.value(QStringLiteral("items")).toArray();
        for (const auto& v : its) {
            if (v.isObject()) db.items.append(LaunchItem::fromJson(v.toObject()));
        }
        return db;
    }
};

struct PluginSettings {
    QString popupHotkey = QStringLiteral("Alt+1");
    bool hideOnBlur = true;
    bool hideAfterLaunch = true;
    bool singleClickLaunch = true;
    bool showAtCursor = false;
    bool topMost = true;
    QString viewMode = QStringLiteral("grid");
    QString tileSize = QStringLiteral("medium");

    static PluginSettings fromJson(const QJsonObject& obj) {
        PluginSettings s;
        if (obj.contains(QStringLiteral("popupHotkey")))
            s.popupHotkey = obj.value(QStringLiteral("popupHotkey")).toString().trimmed();
        if (obj.contains(QStringLiteral("hideOnBlur")))
            s.hideOnBlur = obj.value(QStringLiteral("hideOnBlur")).toBool(true);
        if (obj.contains(QStringLiteral("hideAfterLaunch")))
            s.hideAfterLaunch = obj.value(QStringLiteral("hideAfterLaunch")).toBool(true);
        if (obj.contains(QStringLiteral("singleClickLaunch")))
            s.singleClickLaunch = obj.value(QStringLiteral("singleClickLaunch")).toBool(true);
        if (obj.contains(QStringLiteral("showAtCursor")))
            s.showAtCursor = obj.value(QStringLiteral("showAtCursor")).toBool(false);
        if (obj.contains(QStringLiteral("topMost")))
            s.topMost = obj.value(QStringLiteral("topMost")).toBool(true);
        const QString vm = obj.value(QStringLiteral("viewMode")).toString();
        if (vm == QLatin1String("grid") || vm == QLatin1String("list")) s.viewMode = vm;
        const QString ts = obj.value(QStringLiteral("tileSize")).toString();
        if (ts == QLatin1String("small") || ts == QLatin1String("medium") || ts == QLatin1String("large")) s.tileSize = ts;
        return s;
    }
};
inline QString defaultPageBaseHex(bool dark)
{
    return dark ? QStringLiteral("#FF1C1E22") : QStringLiteral("#FFF8F9FB");
}

inline QString formatOpaqueHex(const QColor& color)
{
    QColor c = color.isValid() ? color : QColor(Qt::black);
    c.setAlpha(255);
    return c.name(QColor::HexArgb).toUpper();
}

inline QColor blendColors(const QColor& fg, const QColor& bg, double fgWeight)
{
    const QColor safeBg = bg.isValid() ? bg : QColor(Qt::black);
    if (!fg.isValid()) {
        QColor out = safeBg;
        out.setAlpha(255);
        return out;
    }
    const double w = std::clamp(fgWeight, 0.0, 1.0);
    const int r = std::clamp(qRound(fg.red() * w + safeBg.red() * (1.0 - w)), 0, 255);
    const int g = std::clamp(qRound(fg.green() * w + safeBg.green() * (1.0 - w)), 0, 255);
    const int b = std::clamp(qRound(fg.blue() * w + safeBg.blue() * (1.0 - w)), 0, 255);
    return QColor(r, g, b, 255);
}

inline QString blendOverOpaque(const QString& fgHex, const QString& bgHex)
{
    const QColor fg(fgHex);
    const QColor bg(bgHex);
    if (!fg.isValid()) {
        return formatOpaqueHex(bg);
    }
    if (fg.alpha() >= 255) {
        return formatOpaqueHex(fg);
    }
    return formatOpaqueHex(blendColors(fg, bg, static_cast<double>(fg.alpha()) / 255.0));
}

struct ThemeData {
    bool dark = true;
    bool rounded = true;
    bool rememberWindowSize = true;
    QString locale = QStringLiteral("zh");
    double cardRadius = 10.0;
    double controlRadius = 6.0;
    QString background = QStringLiteral("#FF1C1D20");
    QString layer = QStringLiteral("#FF25262B");
    QString page = QStringLiteral("#FF202125");
    QString windowEdge = QStringLiteral("#FF3A3C42");
    QString divider = QStringLiteral("#FF32343A");
    QString text = QStringLiteral("#FFF2F2F5");
    QString subtext = QStringLiteral("#FFA8ABB3");
    QString faint = QStringLiteral("#FF787B84");
    QString hover = QStringLiteral("#FF2F3138");
    QString pressed = QStringLiteral("#FF393B43");
    QString control = QStringLiteral("#FF2C2E34");
    QString controlHover = QStringLiteral("#FF363840");
    QString controlBorder = QStringLiteral("#FF454852");
    QString inputFocused = QStringLiteral("#FF2AA8FF");
    QString menuBackground = QStringLiteral("#FF26272C");
    QString menuHover = QStringLiteral("#FF35373F");
    QString accent = QStringLiteral("#FF38B2FF");
    QString accentFill = QStringLiteral("#FF1F78D1");
    QString accentText = QStringLiteral("#FFFFFFFF");
    QString danger = QStringLiteral("#FFE5534B");

    static ThemeData lightDefaults() {
        ThemeData t;
        t.dark = false;
        t.background = QStringLiteral("#FFF5F6F8");
        t.layer = QStringLiteral("#FFFFFFFF");
        t.page = QStringLiteral("#FFF8F9FB");
        t.windowEdge = QStringLiteral("#FFD5D9E2");
        t.divider = QStringLiteral("#FFE6E9EF");
        t.text = QStringLiteral("#FF1E2024");
        t.subtext = QStringLiteral("#FF5E636E");
        t.faint = QStringLiteral("#FF8C919C");
        t.hover = QStringLiteral("#FFEDEFF4");
        t.pressed = QStringLiteral("#FFE2E6EE");
        t.control = QStringLiteral("#FFFFFFFF");
        t.controlHover = QStringLiteral("#FFF1F3F6");
        t.controlBorder = QStringLiteral("#FFD5D9E2");
        t.inputFocused = QStringLiteral("#FF0078D4");
        t.menuBackground = QStringLiteral("#FFFFFFFF");
        t.menuHover = QStringLiteral("#FFEFF2F6");
        t.accent = QStringLiteral("#FF0078D4");
        t.accentFill = QStringLiteral("#FF006CBE");
        t.accentText = QStringLiteral("#FFFFFFFF");
        t.danger = QStringLiteral("#FFC42B1C");
        return t;
    }

    static ThemeData fromAppearance(const QString& localeStr, const QString& themeStr, const QJsonObject& app) {
        const QJsonObject look = app.value(QStringLiteral("nativeLook")).toObject();
        const bool isDark = look.contains(QStringLiteral("dark"))
            ? look.value(QStringLiteral("dark")).toBool(true)
            : (themeStr != QLatin1String("light"));
        ThemeData t = isDark ? ThemeData{} : lightDefaults();
        t.dark = isDark;
        if (!localeStr.isEmpty()) t.locale = localeStr;
        if (app.contains(QStringLiteral("locale")))
            t.locale = app.value(QStringLiteral("locale")).toString(t.locale);
        t.rememberWindowSize = app.value(QStringLiteral("rememberWindowSize")).toBool(true);
        if (look.isEmpty()) {
            const QString corners = app.value(QStringLiteral("corners")).toString();
            if (corners == QLatin1String("square")) {
                t.rounded = false;
                t.cardRadius = 0.0;
                t.controlRadius = 0.0;
            }
        } else {
            t.rounded = look.value(QStringLiteral("rounded")).toBool(true);
            t.cardRadius = t.rounded ? look.value(QStringLiteral("cardRadius")).toDouble(10.0) : 0.0;
            t.controlRadius = t.rounded ? look.value(QStringLiteral("controlRadius")).toDouble(6.0) : 0.0;
            auto pick = [&](const char* key, QString& dst) {
                const QString v = look.value(QLatin1String(key)).toString();
                if (v.startsWith(u'#') && (v.size() == 7 || v.size() == 9) && QColor(v).isValid()) {
                    dst = v;
                }
            };
            pick("background", t.background);
            pick("layer", t.layer);
            pick("page", t.page);
            pick("windowEdge", t.windowEdge);
            pick("divider", t.divider);
            pick("text", t.text);
            pick("subtext", t.subtext);
            pick("faint", t.faint);
            pick("hover", t.hover);
            pick("pressed", t.pressed);
            pick("control", t.control);
            pick("controlHover", t.controlHover);
            pick("controlBorder", t.controlBorder);
            pick("inputFocused", t.inputFocused);
            pick("menuBackground", t.menuBackground);
            pick("menuHover", t.menuHover);
            pick("accent", t.accent);
            pick("accentFill", t.accentFill);
            pick("accentText", t.accentText);
            pick("danger", t.danger);
        }

        t.page = blendOverOpaque(t.page, defaultPageBaseHex(t.dark));
        t.background = blendOverOpaque(t.background, t.page);
        t.layer = blendOverOpaque(t.layer, t.page);
        t.menuBackground = blendOverOpaque(t.menuBackground, t.layer);
        t.control = blendOverOpaque(t.control, t.layer);
        t.windowEdge = blendOverOpaque(t.windowEdge, t.page);
        t.divider = blendOverOpaque(t.divider, t.page);
        t.controlBorder = blendOverOpaque(t.controlBorder, t.page);
        if (!t.dark) {
            const QColor pageColor(t.page);
            auto softenIntoPage = [&](QString& hex, int minLightness, double weight) {
                const QColor c(hex);
                if (c.isValid() && c.lightness() < minLightness) {
                    hex = formatOpaqueHex(blendColors(c, pageColor, weight));
                }
            };
            softenIntoPage(t.windowEdge, 150, 0.22);
            softenIntoPage(t.divider, 190, 0.10);
            softenIntoPage(t.controlBorder, 150, 0.22);
        }
        return t;
    }
};
struct SearchMatch {
    LaunchItem item;
    QString categoryName;
    int score = 0;
};

struct MayeImportResult {
    int addedCategories = 0;
    int addedItems = 0;
    int skippedDuplicates = 0;
    QStringList warnings;
};

struct HotkeyStatus {
    QString key;
    bool registered = false;
    bool viaHost = false;
    int errorCode = 0;
    QString errorMessage;
};

struct ResolvedShortcut {
    QString target;
    QString arguments;
    QString workingDir;
    QString iconPath;
    bool runAsAdmin = false;
};

} // namespace ql
