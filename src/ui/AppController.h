#pragma once

#include "core/LauncherStore.h"
#include "core/Models.h"
#include "core/PinyinMatcher.h"
#include "platform/HotkeyService.h"
#include "ui/LauncherModels.h"
#include "ui/ThemeObject.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class QQmlApplicationEngine;
class QQuickWindow;
class QTimer;

namespace ql {

class IconProvider;
class Protocol;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(ql::ThemeObject* theme READ theme CONSTANT)
    Q_PROPERTY(ql::CategoryListModel* categoryModel READ categoryModel CONSTANT)
    Q_PROPERTY(ql::LaunchItemModel* itemModel READ itemModel CONSTANT)
    Q_PROPERTY(QString activeCategoryId READ activeCategoryId WRITE setActiveCategoryId NOTIFY activeCategoryIdChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QVariantList categoryChoices READ categoryChoices NOTIFY databaseChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY settingsChanged)
    Q_PROPERTY(QString tileSize READ tileSize WRITE setTileSize NOTIFY settingsChanged)
    Q_PROPERTY(bool hideOnBlur READ hideOnBlur NOTIFY settingsChanged)
    Q_PROPERTY(bool hideAfterLaunch READ hideAfterLaunch NOTIFY settingsChanged)
    Q_PROPERTY(bool singleClickLaunch READ singleClickLaunch NOTIFY settingsChanged)
    Q_PROPERTY(bool showAtCursor READ showAtCursor NOTIFY settingsChanged)
    Q_PROPERTY(bool topMost READ topMost NOTIFY settingsChanged)
    Q_PROPERTY(QString hotkeyKey READ hotkeyKey NOTIFY settingsChanged)
    Q_PROPERTY(bool hotkeyRegistered READ hotkeyRegistered NOTIFY hotkeyStatusChanged)
    Q_PROPERTY(QString hotkeyErrorText READ hotkeyErrorText NOTIFY hotkeyStatusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool hasDefaultMayeFile READ hasDefaultMayeFile NOTIFY mayeDefaultChanged)
    Q_PROPERTY(double windowWidth READ windowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(double windowHeight READ windowHeight NOTIFY windowSizeChanged)
    Q_PROPERTY(bool dialogOpen READ dialogOpen WRITE setDialogOpen NOTIFY dialogOpenChanged)
    Q_PROPERTY(bool standaloneMode READ standaloneMode CONSTANT)
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)

public:
    explicit AppController(Protocol* protocol, QString dataDir, bool standaloneMode, QObject* parent = nullptr);
    ~AppController() override;

    ThemeObject* theme() const { return m_theme; }
    CategoryListModel* categoryModel() const { return m_categoryModel; }
    LaunchItemModel* itemModel() const { return m_itemModel; }

    QString activeCategoryId() const { return m_activeCategoryId; }
    void setActiveCategoryId(const QString& categoryId);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString& text);
    QVariantList categoryChoices() const;

    QString viewMode() const { return m_settings.viewMode; }
    void setViewMode(const QString& mode);
    QString tileSize() const { return m_settings.tileSize; }
    void setTileSize(const QString& size);

    bool hideOnBlur() const { return m_settings.hideOnBlur; }
    bool hideAfterLaunch() const { return m_settings.hideAfterLaunch; }
    bool singleClickLaunch() const { return m_settings.singleClickLaunch; }
    bool showAtCursor() const { return m_settings.showAtCursor; }
    bool topMost() const { return m_settings.topMost; }
    QString hotkeyKey() const { return m_settings.popupHotkey; }

    bool hotkeyRegistered() const { return m_hotkeyStatus.registered; }
    QString hotkeyErrorText() const { return m_hotkeyStatus.errorMessage; }

    QString statusMessage() const { return m_statusMessage; }
    bool statusError() const { return m_statusError; }
    bool hasDefaultMayeFile() const;

    double windowWidth() const { return m_windowWidth; }
    double windowHeight() const { return m_windowHeight; }

    bool dialogOpen() const { return m_dialogOpen; }
    void setDialogOpen(bool open);
    bool standaloneMode() const { return m_standaloneMode; }
    bool shuttingDown() const { return m_shuttingDown; }

    Q_INVOKABLE void showPopup();
    Q_INVOKABLE void hidePopup();
    Q_INVOKABLE void togglePopup();
    Q_INVOKABLE void showSettingsDialog();
    Q_INVOKABLE void handleWindowDeactivated();
    Q_INVOKABLE void onWindowResized(double width, double height);
    Q_INVOKABLE void pauseHotkeysForRecording(bool paused);

    Q_INVOKABLE void selectCategory(const QString& categoryId);
    Q_INVOKABLE void cycleCategory(int step);
    Q_INVOKABLE bool addCategory(const QString& name);
    Q_INVOKABLE bool renameCategory(const QString& categoryId, const QString& newName);
    Q_INVOKABLE bool deleteCategory(const QString& categoryId);
    Q_INVOKABLE bool moveCategoryOrder(const QString& categoryId, int delta);

    Q_INVOKABLE void launchItem(const QString& itemId, bool forceAdmin = false);
    Q_INVOKABLE void launchItemAt(int row, bool forceAdmin = false);
    Q_INVOKABLE void revealItem(const QString& itemId);
    Q_INVOKABLE void copyItemTarget(const QString& itemId);
    Q_INVOKABLE QVariantMap getItemDetails(const QString& itemId) const;
    Q_INVOKABLE bool saveItem(const QVariantMap& data);
    Q_INVOKABLE bool deleteItem(const QString& itemId);
    Q_INVOKABLE bool moveItemToCategory(const QString& itemId, const QString& categoryId);
    Q_INVOKABLE bool moveItemOrder(const QString& itemId, int delta);
    Q_INVOKABLE int addDroppedUrls(const QList<QUrl>& urls, const QString& targetCategoryId = QString());

    Q_INVOKABLE QString pickFile(const QString& title, const QString& filterName = QString(), const QString& filterPattern = QString());
    Q_INVOKABLE QString pickFolder(const QString& title);
    Q_INVOKABLE void clearStatus();
    Q_INVOKABLE void setStatus(const QString& message, bool isError = false);

    Q_INVOKABLE void applySettings(const QVariantMap& map);
    Q_INVOKABLE QString defaultMayePath() const;
    Q_INVOKABLE bool importMayeDefault();
    Q_INVOKABLE bool importMayeFromDialog();
    Q_INVOKABLE bool exportBackupDialog();
    Q_INVOKABLE bool importBackupDialog();
    Q_INVOKABLE void openDataDir();
signals:
    void popupShown();
    void focusSearchRequested();
    void openSettingsRequested();
    void activeCategoryIdChanged();
    void searchTextChanged();
    void databaseChanged();
    void settingsChanged();
    void hotkeyStatusChanged();
    void statusChanged();
    void mayeDefaultChanged();
    void windowSizeChanged();
    void dialogOpenChanged();
    void shuttingDownChanged();

private:
    void initializeStorage(const QString& dataDir, bool hostHotkeys);
    void beginShutdown();
    bool ensureWindow();
    void refreshModels();
    void refreshItemFilter();
    void prewarmIcons();
    void pushSettingsUpdate();
    void loadStandaloneSettings();
    void saveStandaloneSettings() const;
    QJsonObject settingsToJson() const;
    QString resolveTargetCategoryForInsert(const QString& requestedCategoryId) const;
    QJsonObject buildQueryItem(const LaunchItem& item, const QString& categoryName, int score) const;

    void handleMessage(const QJsonValue& id, const QString& method, const QJsonObject& params, bool hasId);
    void handleInitialize(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleQuery(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleAction(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleTrayMenu(const QJsonValue& id, bool hasId);
    void handleMenu(const QJsonObject& params);
    void handleSettingsChanged(const QJsonObject& params);
    void handleAppearance(const QJsonObject& params);
    void handleHotkey(const QJsonObject& params);
    void handleShutdown(const QJsonValue& id, bool hasId);

    QPointer<Protocol> m_protocol;
    QString m_pluginDir;
    QString m_dataDir;
    bool m_standaloneMode = false;
    bool m_initialized = false;
    bool m_dialogOpen = false;
    bool m_pickingFile = false;
    bool m_hotkeysPaused = false;
    bool m_shuttingDown = false;

    ThemeObject* m_theme = nullptr;
    CategoryListModel* m_categoryModel = nullptr;
    LaunchItemModel* m_itemModel = nullptr;
    std::unique_ptr<LauncherStore> m_store;
    std::unique_ptr<HotkeyService> m_hotkeyService;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QPointer<QQuickWindow> m_window;
    IconProvider* m_iconProvider = nullptr;

    LauncherDatabase m_cachedDb;
    PinyinMatcher::SearchIndex m_searchIndex;
    PluginSettings m_settings;
    HotkeyStatus m_hotkeyStatus;
    QString m_activeCategoryId = QStringLiteral("__all__");
    QString m_searchText;
    QString m_statusMessage;
    bool m_statusError = false;
    double m_windowWidth = 720.0;
    double m_windowHeight = 490.0;
    bool m_userResized = false;
    QElapsedTimer m_lastShownTimer;
    QTimer* m_resizeSaveTimer = nullptr;
};

} // namespace ql
