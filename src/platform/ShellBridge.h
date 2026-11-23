#pragma once

#include "core/Models.h"

#include <QString>

#include <optional>

class QWindow;

namespace ql {

class ShellBridge {
public:
    static QString expandPath(const QString& raw);
    static bool isWebUrl(const QString& target);
    static bool isSpecialSystemUri(const QString& target);
    static QString resolveFullPath(const QString& target);
    static bool existsOrResolvable(const QString& target);

    static std::optional<ResolvedShortcut> resolveLnk(const QString& lnkPath);
    static QString ensureLaunchLnk(
        const QString& dataDir, const LaunchItem& item, bool forceAdmin = false, QString* error = nullptr);
    static bool needsShimLnk(const LaunchItem& item, bool forceAdmin);

    static QString pickOpenFile(
        QWindow* owner, const QString& title, const QString& filterName,
        const QString& filterPattern, const QString& initialDir = {});
    static QString pickSaveFile(
        QWindow* owner, const QString& title, const QString& filterName,
        const QString& filterPattern, const QString& defaultName, const QString& initialDir = {});
    static QString pickFolder(QWindow* owner, const QString& title, const QString& initialDir = {});

    static void applyWindowChrome(QWindow* window, const ThemeData& theme);
    static void positionWindow(QWindow* window, bool nearCursor);
    static void activateWindow(QWindow* window);

    static bool standaloneLaunch(const LaunchItem& item, const QString& dataDir, bool forceAdmin, QString* error);
    static bool standaloneReveal(const LaunchItem& item, QString* error);
};

} // namespace ql
