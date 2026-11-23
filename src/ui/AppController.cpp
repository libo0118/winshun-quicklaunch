#include "ui/AppController.h"

#include "core/MayeNanoImporter.h"
#include "core/PinyinMatcher.h"
#include "platform/IconProvider.h"
#include "platform/ShellBridge.h"
#include "protocol/Protocol.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSizeF>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace ql {

namespace {

QString displayNameFromPath(const QString& rawPath)
{
    const QFileInfo fi(rawPath);
    QString name = fi.completeBaseName().trimmed();
    if (name.isEmpty()) {
        name = fi.fileName().trimmed();
    }
    if (name.isEmpty()) {
        name = QDir::toNativeSeparators(rawPath);
    }
    return name;
}

LaunchItem makeItemFromLocalPath(const QString& filePath, const QString& categoryId)
{
    LaunchItem item;
    item.categoryId = categoryId;
    item.name = displayNameFromPath(filePath);
    const QString native = QDir::toNativeSeparators(filePath.trimmed());
    if (native.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive)) {
        if (const auto resolved = ShellBridge::resolveLnk(native)) {
            item.target = resolved->target;
            item.arguments = resolved->arguments;
            item.workingDir = resolved->workingDir;
            item.iconPath = resolved->iconPath;
            item.runAsAdmin = resolved->runAsAdmin;
            return item;
        }
    }
    item.target = native;
    return item;
}

QSizeF resolveWindowSize(double rawW, double rawH, bool userResized)
{
    if (!userResized && qFuzzyCompare(rawW, 760.0) && qFuzzyCompare(rawH, 520.0)) {
        return QSizeF(720.0, 490.0);
    }
    return QSizeF(qBound(500.0, rawW, 1600.0), qBound(340.0, rawH, 1100.0));
}

} // namespace

AppController::AppController(Protocol* protocol, QString dataDir, bool standaloneMode, QObject* parent)
    : QObject(parent)
    , m_protocol(protocol)
    , m_dataDir(std::move(dataDir))
    , m_standaloneMode(standaloneMode)
    , m_theme(new ThemeObject(this))
    , m_categoryModel(new CategoryListModel(this))
    , m_itemModel(new LaunchItemModel(this))
    , m_resizeSaveTimer(new QTimer(this))
{
    m_resizeSaveTimer->setSingleShot(true);
    m_resizeSaveTimer->setInterval(250);
    connect(m_resizeSaveTimer, &QTimer::timeout, this, [this]() {
        if (m_store && m_theme->rememberWindowSize()) {
            m_store->saveWindowSize(m_windowWidth, m_windowHeight);
        }
    });
    if (m_protocol) {
        connect(m_protocol, &Protocol::requested, this,
            [this](const QJsonValue& id, const QString& method, const QJsonObject& params) {
                handleMessage(id, method, params, true);
            });
        connect(m_protocol, &Protocol::notified, this,
            [this](const QString& method, const QJsonObject& params) {
                handleMessage(QJsonValue{}, method, params, false);
            });
        connect(m_protocol, &Protocol::closed, this, [this]() {
            if (m_hotkeyService) {
                m_hotkeyService->releaseAll();
            }
            if (m_window) {
                m_window->hide();
                m_window->close();
            }
            QCoreApplication::quit();
        });
    } else if (m_standaloneMode) {
        if (m_dataDir.trimmed().isEmpty()) {
            m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        }
        loadStandaloneSettings();
        initializeStorage(m_dataDir, false);
        showPopup();
    }
}

AppController::~AppController()
{
    if (m_hotkeyService) {
        m_hotkeyService->releaseAll();
    }
    m_iconProvider = nullptr;
    m_engine.reset();
}

void AppController::initializeStorage(const QString& dataDir, bool hostHotkeys)
{
    m_dataDir = dataDir.trimmed();
    if (m_dataDir.isEmpty()) {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    m_store = std::make_unique<LauncherStore>(m_dataDir, this);
    connect(m_store.get(), &LauncherStore::changed, this, &AppController::refreshModels);

    const LauncherDatabase db = m_store->loadOrInitialize();
    m_activeCategoryId = db.activeCategoryId.isEmpty() ? QStringLiteral("__all__") : db.activeCategoryId;
    if (db.categories.size() == 1 && m_activeCategoryId == QLatin1String("__all__")) {
        m_activeCategoryId = db.categories.first().id;
    }
    const QSizeF sz = resolveWindowSize(db.windowWidth, db.windowHeight, m_userResized);
    m_windowWidth = sz.width();
    m_windowHeight = sz.height();

    m_hotkeyService = std::make_unique<HotkeyService>(m_protocol.data(), hostHotkeys, this);
    connect(m_hotkeyService.get(), &HotkeyService::triggered, this, &AppController::togglePopup);
    connect(m_hotkeyService.get(), &HotkeyService::statusChanged, this, [this](const HotkeyStatus& st) {
        m_hotkeyStatus = st;
        emit hotkeyStatusChanged();
        if (!st.registered && !st.key.trimmed().isEmpty() && !st.errorMessage.isEmpty()) {
            setStatus(st.errorMessage, true);
        } else if (st.registered && m_statusError && m_statusMessage.contains(QLatin1String("热键"))) {
            clearStatus();
        }
    });
    m_hotkeyService->applyHotkey(m_settings.popupHotkey);
    m_initialized = true;
    refreshModels();
    emit windowSizeChanged();
    emit mayeDefaultChanged();
}
bool AppController::ensureWindow()
{
    if (m_window) {
        return true;
    }
    QGuiApplication::setQuitOnLastWindowClosed(m_standaloneMode);
    m_engine = std::make_unique<QQmlApplicationEngine>(this);
    const QString hostQmlDir = qEnvironmentVariable("WS_QT_QML_DIR");
    if (!hostQmlDir.isEmpty()) {
        m_engine->addImportPath(hostQmlDir);
    }
    auto* provider = new IconProvider();
    m_iconProvider = provider;
    m_engine->addImageProvider(QStringLiteral("appicon"), provider);
    m_engine->rootContext()->setContextProperty(QStringLiteral("controller"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    m_engine->load(QUrl(QStringLiteral("qrc:/ql/MainWindow.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        if (m_protocol) {
            m_protocol->log(QStringLiteral("error"), QStringLiteral("Failed to load qrc:/ql/MainWindow.qml"));
        }
        return false;
    }
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    if (!m_window) {
        return false;
    }
    if (m_theme->rememberWindowSize()) {
        m_window->setWidth(qRound(m_windowWidth));
        m_window->setHeight(qRound(m_windowHeight));
    }
    ShellBridge::applyWindowChrome(m_window.data(), m_theme->data());
    prewarmIcons();
    return true;
}

void AppController::refreshModels()
{
    if (!m_store) {
        return;
    }
    m_cachedDb = m_store->snapshot();
    m_searchIndex = PinyinMatcher::buildIndex(m_cachedDb);
    m_categoryModel->refresh(m_cachedDb);

    QString nextCat = m_cachedDb.activeCategoryId;
    if (nextCat.isEmpty()) {
        nextCat = QStringLiteral("__all__");
    }
    auto catExists = [&](const QString& id) {
        for (const auto& c : m_cachedDb.categories) {
            if (c.id == id) return true;
        }
        return false;
    };
    if (m_cachedDb.categories.size() == 1) {
        if (nextCat == QLatin1String("__all__") || !catExists(nextCat)) {
            nextCat = m_cachedDb.categories.first().id;
        }
    } else if (nextCat != QLatin1String("__all__") && !catExists(nextCat)) {
        nextCat = QStringLiteral("__all__");
    }
    if (nextCat != m_activeCategoryId) {
        m_activeCategoryId = nextCat;
        emit activeCategoryIdChanged();
    }
    const QSizeF sz = resolveWindowSize(m_cachedDb.windowWidth, m_cachedDb.windowHeight, m_userResized);
    if (!qFuzzyCompare(sz.width(), m_windowWidth) || !qFuzzyCompare(sz.height(), m_windowHeight)) {
        m_windowWidth = sz.width();
        m_windowHeight = sz.height();
        emit windowSizeChanged();
    }
    emit databaseChanged();
    refreshItemFilter();
    prewarmIcons();
}

void AppController::refreshItemFilter()
{
    if (!m_store) {
        return;
    }
    const QList<SearchMatch> matches =
        PinyinMatcher::search(m_searchIndex, m_searchText, m_activeCategoryId, true);
    m_itemModel->setMatches(matches);
}

void AppController::prewarmIcons()
{
    if (!m_iconProvider || m_cachedDb.items.isEmpty()) {
        return;
    }
    QStringList keys;
    keys.reserve(m_cachedDb.items.size());
    for (const auto& it : m_cachedDb.items) {
        const QString key = it.iconPath.trimmed().isEmpty()
            ? it.target.trimmed()
            : it.iconPath.trimmed();
        if (!key.isEmpty()) {
            keys.append(key);
        }
    }
    if (!keys.isEmpty()) {
        m_iconProvider->prewarmAsync(keys);
    }
}

void AppController::setActiveCategoryId(const QString& categoryId)
{
    QString next = categoryId.trimmed().isEmpty() ? QStringLiteral("__all__") : categoryId.trimmed();
    if (m_cachedDb.categories.size() == 1 && next == QLatin1String("__all__")) {
        next = m_cachedDb.categories.first().id;
    }
    if (next == m_activeCategoryId) {
        return;
    }
    m_activeCategoryId = next;
    m_cachedDb.activeCategoryId = m_activeCategoryId;
    emit activeCategoryIdChanged();
    if (m_store) {
        m_store->setActiveCategory(m_activeCategoryId);
    }
    refreshItemFilter();
}

void AppController::setSearchText(const QString& text)
{
    if (text == m_searchText) {
        return;
    }
    m_searchText = text;
    emit searchTextChanged();
    refreshItemFilter();
}

QVariantList AppController::categoryChoices() const
{
    QVariantList list;
    if (!m_store) {
        return list;
    }
    const LauncherDatabase& db = m_cachedDb;
    for (const auto& c : db.categories) {
        list.append(QVariantMap{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("name"), c.name},
        });
    }
    return list;
}

void AppController::setViewMode(const QString& mode)
{
    const QString normalized = (mode == QLatin1String("list")) ? QStringLiteral("list") : QStringLiteral("grid");
    if (m_settings.viewMode == normalized) {
        return;
    }
    m_settings.viewMode = normalized;
    emit settingsChanged();
    pushSettingsUpdate();
}

void AppController::setTileSize(const QString& size)
{
    const QString normalized = (size == QLatin1String("small") || size == QLatin1String("large"))
        ? size
        : QStringLiteral("medium");
    if (m_settings.tileSize == normalized) {
        return;
    }
    m_settings.tileSize = normalized;
    emit settingsChanged();
    pushSettingsUpdate();
}

void AppController::setDialogOpen(bool open)
{
    if (m_dialogOpen == open) {
        return;
    }
    m_dialogOpen = open;
    emit dialogOpenChanged();
}
void AppController::showPopup()
{
    if (!m_initialized && m_standaloneMode) {
        initializeStorage(m_dataDir, false);
    }
    if (!ensureWindow()) {
        return;
    }
    if (!m_searchText.isEmpty()) {
        m_searchText.clear();
        emit searchTextChanged();
        refreshItemFilter();
    }
    const bool wasVisible = m_window->isVisible();
    if (!wasVisible && m_theme->rememberWindowSize()) {
        m_window->setWidth(qRound(m_windowWidth));
        m_window->setHeight(qRound(m_windowHeight));
    }
    if (!wasVisible || m_settings.showAtCursor) {
        ShellBridge::positionWindow(m_window.data(), m_settings.showAtCursor);
    }
    m_lastShownTimer.restart();
    ShellBridge::activateWindow(m_window.data());
    emit popupShown();
    emit focusSearchRequested();
}

void AppController::hidePopup()
{
    if (!m_window) {
        return;
    }
    if (m_hotkeysPaused) {
        pauseHotkeysForRecording(false);
    }
    if (m_dialogOpen) {
        setDialogOpen(false);
    }
    m_window->hide();
}

void AppController::togglePopup()
{
    if (m_window && m_window->isVisible()) {
        hidePopup();
    } else {
        showPopup();
    }
}

void AppController::showSettingsDialog()
{
    showPopup();
    emit openSettingsRequested();
}

void AppController::handleWindowDeactivated()
{
    if (m_lastShownTimer.isValid() && m_lastShownTimer.elapsed() < 200) {
        return;
    }
    if (m_standaloneMode || !m_settings.hideOnBlur || m_dialogOpen || m_pickingFile || !m_window) {
        return;
    }
    QTimer::singleShot(110, this, [this]() {
        if (!m_window || !m_window->isVisible() || m_dialogOpen || m_pickingFile || !m_settings.hideOnBlur) {
            return;
        }
        if (QGuiApplication::focusWindow() != nullptr || m_window->isActive()) {
            return;
        }
        hidePopup();
    });
}

void AppController::onWindowResized(double width, double height)
{
    const double w = qBound(500.0, width, 1600.0);
    const double h = qBound(340.0, height, 1100.0);
    if (qFuzzyCompare(w, m_windowWidth) && qFuzzyCompare(h, m_windowHeight)) {
        return;
    }
    m_userResized = true;
    m_windowWidth = w;
    m_windowHeight = h;
    if (m_theme->rememberWindowSize()) {
        m_resizeSaveTimer->start();
    }
}
void AppController::pauseHotkeysForRecording(bool paused)
{
    if (m_hotkeysPaused == paused) {
        return;
    }
    m_hotkeysPaused = paused;
    if (!m_hotkeyService) {
        return;
    }
    if (paused) {
        m_hotkeyService->releaseAll();
    } else {
        m_hotkeyService->applyHotkey(m_settings.popupHotkey);
    }
}

void AppController::selectCategory(const QString& categoryId)
{
    setActiveCategoryId(categoryId);
}

void AppController::cycleCategory(int step)
{
    const int count = m_categoryModel->rowCount();
    if (count <= 0) {
        return;
    }
    int cur = m_categoryModel->indexOfId(m_activeCategoryId);
    if (cur < 0) {
        cur = 0;
    }
    const int next = (cur + step % count + count) % count;
    const QString nextId = m_categoryModel->idAt(next);
    if (!nextId.isEmpty()) {
        setActiveCategoryId(nextId);
    }
}

bool AppController::addCategory(const QString& name)
{
    if (!m_store) {
        return false;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(QStringLiteral("分组名称不能为空"), true);
        return false;
    }
    const CategoryEntry created = m_store->addCategory(trimmed);
    setActiveCategoryId(created.id);
    setStatus(QStringLiteral("已添加分组：%1").arg(created.name), false);
    return true;
}

bool AppController::renameCategory(const QString& categoryId, const QString& newName)
{
    if (!m_store || categoryId == QLatin1String("__all__")) {
        return false;
    }
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(QStringLiteral("分组名称不能为空"), true);
        return false;
    }
    if (!m_store->renameCategory(categoryId, trimmed)) {
        setStatus(QStringLiteral("重命名分组失败"), true);
        return false;
    }
    setStatus(QStringLiteral("已重命名分组为：%1").arg(trimmed), false);
    return true;
}

bool AppController::deleteCategory(const QString& categoryId)
{
    if (!m_store || categoryId == QLatin1String("__all__")) {
        return false;
    }
    if (!m_store->deleteCategory(categoryId, true)) {
        setStatus(QStringLiteral("至少需要保留一个分组"), true);
        return false;
    }
    if (m_activeCategoryId == categoryId) {
        setActiveCategoryId(QStringLiteral("__all__"));
    }
    setStatus(QStringLiteral("已删除分组（条目已移至默认分组）"), false);
    return true;
}
bool AppController::moveCategoryOrder(const QString& categoryId, int delta)
{
    if (!m_store || categoryId == QLatin1String("__all__")) {
        return false;
    }
    return m_store->moveCategoryOrder(categoryId, delta);
}

QString AppController::resolveTargetCategoryForInsert(const QString& requestedCategoryId) const
{
    const QString req = requestedCategoryId.trimmed();
    if (!req.isEmpty() && req != QLatin1String("__all__")) {
        return req;
    }
    if (!m_activeCategoryId.isEmpty() && m_activeCategoryId != QLatin1String("__all__")) {
        return m_activeCategoryId;
    }
    if (m_store) {
        const LauncherDatabase db = m_store->snapshot();
        if (!db.categories.isEmpty()) {
            return db.categories.first().id;
        }
    }
    return QStringLiteral("default");
}

void AppController::launchItem(const QString& itemId, bool forceAdmin)
{
    if (!m_store) {
        return;
    }
    const std::optional<LaunchItem> found = m_store->findById(itemId);
    if (!found) {
        setStatus(QStringLiteral("未找到要启动的项目"), true);
        return;
    }
    const QString expanded = ShellBridge::expandPath(found->target);
    if (!ShellBridge::existsOrResolvable(expanded)) {
        setStatus(QStringLiteral("目标不存在：%1").arg(found->target), true);
        return;
    }

    if (m_protocol) {
        if (ShellBridge::isWebUrl(expanded)) {
            m_store->recordLaunch(found->id);
            m_protocol->tool(QStringLiteral("open_url"),
                QJsonObject{{QStringLiteral("url"), expanded}},
                [this](const QJsonValue&, const QString& err) {
                    if (!err.isEmpty()) {
                        setStatus(QStringLiteral("打开网址失败：%1").arg(err), true);
                    } else {
                        clearStatus();
                        if (m_settings.hideAfterLaunch) {
                            hidePopup();
                        }
                    }
                });
            return;
        }
        QString launchPath;
        if (ShellBridge::needsShimLnk(*found, forceAdmin)) {
            QString err;
            launchPath = ShellBridge::ensureLaunchLnk(m_dataDir, *found, forceAdmin, &err);
            if (launchPath.isEmpty()) {
                setStatus(err.isEmpty() ? QStringLiteral("创建启动快捷方式失败") : err, true);
                return;
            }
        } else {
            launchPath = ShellBridge::resolveFullPath(expanded);
            if (launchPath.isEmpty()) {
                launchPath = expanded;
            }
        }
        m_store->recordLaunch(found->id);
        m_protocol->tool(QStringLiteral("open_item"),
            QJsonObject{{QStringLiteral("path"), QDir::toNativeSeparators(launchPath)}},
            [this](const QJsonValue& result, const QString& err) {
                const bool opened = err.isEmpty() && (!result.isObject() || result.toObject().value(QStringLiteral("opened")).toBool(true));
                if (!opened) {
                    setStatus(err.isEmpty() ? QStringLiteral("宿主无法打开目标") : QStringLiteral("启动失败：%1").arg(err), true);
                } else {
                    clearStatus();
                    if (m_settings.hideAfterLaunch) {
                        hidePopup();
                    }
                }
            });
        return;
    }
    QString err;
    if (!ShellBridge::standaloneLaunch(*found, m_dataDir, forceAdmin, &err)) {
        setStatus(err.isEmpty() ? QStringLiteral("启动失败") : err, true);
        return;
    }
    m_store->recordLaunch(found->id);
    setStatus(QStringLiteral("已启动：%1").arg(found->name), false);
    if (m_settings.hideAfterLaunch && !m_standaloneMode) {
        hidePopup();
    }
}

void AppController::launchItemAt(int row, bool forceAdmin)
{
    const QString id = m_itemModel->idAt(row);
    if (!id.isEmpty()) {
        launchItem(id, forceAdmin);
    }
}

void AppController::revealItem(const QString& itemId)
{
    if (!m_store) {
        return;
    }
    const std::optional<LaunchItem> found = m_store->findById(itemId);
    if (!found) {
        setStatus(QStringLiteral("未找到项目"), true);
        return;
    }
    const QString expanded = ShellBridge::expandPath(found->target);
    if (ShellBridge::isWebUrl(expanded) || ShellBridge::isSpecialSystemUri(expanded)) {
        setStatus(QStringLiteral("网址或系统快捷项没有本地所在文件夹"), true);
        return;
    }
    if (m_protocol) {
        const QString resolved = ShellBridge::resolveFullPath(expanded);
        if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
            setStatus(QStringLiteral("找不到目标路径：%1").arg(found->target), true);
            return;
        }
        m_protocol->tool(QStringLiteral("show_in_folder"),
            QJsonObject{{QStringLiteral("path"), QDir::toNativeSeparators(resolved)}},
            [this](const QJsonValue&, const QString& err) {
                if (!err.isEmpty()) {
                    setStatus(QStringLiteral("定位文件夹失败：%1").arg(err), true);
                } else {
                    clearStatus();
                }
            });
        return;
    }
    QString err;
    if (!ShellBridge::standaloneReveal(*found, &err)) {
        setStatus(err.isEmpty() ? QStringLiteral("定位文件夹失败") : err, true);
    } else {
        clearStatus();
    }
}

void AppController::copyItemTarget(const QString& itemId)
{
    if (!m_store) {
        return;
    }
    const std::optional<LaunchItem> found = m_store->findById(itemId);
    if (!found) {
        return;
    }
    const QString expanded = ShellBridge::expandPath(found->target);
    const QString text = (ShellBridge::isWebUrl(expanded) || ShellBridge::isSpecialSystemUri(expanded))
        ? expanded
        : QDir::toNativeSeparators(ShellBridge::resolveFullPath(expanded));
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
    setStatus(QStringLiteral("已复制路径：%1").arg(text), false);
}
QVariantMap AppController::getItemDetails(const QString& itemId) const
{
    if (!m_store || itemId.trimmed().isEmpty()) {
        return {};
    }
    const std::optional<LaunchItem> found = m_store->findById(itemId);
    if (!found) {
        return {};
    }
    return {
        {QStringLiteral("itemId"), found->id},
        {QStringLiteral("categoryId"), found->categoryId},
        {QStringLiteral("name"), found->name},
        {QStringLiteral("target"), found->target},
        {QStringLiteral("arguments"), found->arguments},
        {QStringLiteral("workingDir"), found->workingDir},
        {QStringLiteral("iconPath"), found->iconPath},
        {QStringLiteral("keywords"), found->keywords},
        {QStringLiteral("remark"), found->remark},
        {QStringLiteral("runAsAdmin"), found->runAsAdmin},
    };
}

bool AppController::saveItem(const QVariantMap& data)
{
    if (!m_store) {
        return false;
    }
    const QString target = data.value(QStringLiteral("target")).toString().trimmed();
    QString name = data.value(QStringLiteral("name")).toString().trimmed();
    if (target.isEmpty()) {
        setStatus(QStringLiteral("目标路径或网址不能为空"), true);
        return false;
    }
    if (name.isEmpty()) {
        name = ShellBridge::isWebUrl(target) ? target : displayNameFromPath(target);
    }

    const QString itemId = data.value(QStringLiteral("itemId")).toString().trimmed();
    const QString catId = resolveTargetCategoryForInsert(data.value(QStringLiteral("categoryId")).toString());

    LaunchItem item;
    if (!itemId.isEmpty()) {
        if (const auto existing = m_store->findById(itemId)) {
            item = *existing;
        }
        item.id = itemId;
    }
    item.categoryId = catId;
    item.name = name;
    item.target = target;
    item.arguments = data.value(QStringLiteral("arguments")).toString().trimmed();
    item.workingDir = data.value(QStringLiteral("workingDir")).toString().trimmed();
    item.iconPath = data.value(QStringLiteral("iconPath")).toString().trimmed();
    item.keywords = data.value(QStringLiteral("keywords")).toString().trimmed();
    item.remark = data.value(QStringLiteral("remark")).toString().trimmed();
    item.runAsAdmin = data.value(QStringLiteral("runAsAdmin")).toBool();

    if (item.id.isEmpty()) {
        m_store->addItem(item);
        setStatus(QStringLiteral("已添加：%1").arg(item.name), false);
    } else {
        m_store->updateItem(item);
        setStatus(QStringLiteral("已保存：%1").arg(item.name), false);
    }
    return true;
}

bool AppController::deleteItem(const QString& itemId)
{
    if (!m_store || !m_store->deleteItem(itemId)) {
        return false;
    }
    setStatus(QStringLiteral("已删除启动项"), false);
    return true;
}

bool AppController::moveItemToCategory(const QString& itemId, const QString& categoryId)
{
    if (!m_store || categoryId.isEmpty() || categoryId == QLatin1String("__all__")) {
        return false;
    }
    const bool ok = m_store->moveItemToCategory(itemId, categoryId);
    if (ok) {
        setStatus(QStringLiteral("已移动条目到目标分组"), false);
    }
    return ok;
}
bool AppController::moveItemOrder(const QString& itemId, int delta)
{
    return m_store ? m_store->moveItemOrder(itemId, delta) : false;
}

int AppController::addDroppedUrls(const QList<QUrl>& urls, const QString& targetCategoryId)
{
    if (!m_store || urls.isEmpty()) {
        return 0;
    }
    const QString catId = resolveTargetCategoryForInsert(targetCategoryId);
    QList<LaunchItem> batch;
    batch.reserve(urls.size());
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            const QString local = url.toLocalFile();
            if (!local.trimmed().isEmpty()) {
                batch.append(makeItemFromLocalPath(local, catId));
            }
        } else if (ShellBridge::isWebUrl(url.toString())) {
            LaunchItem item;
            item.categoryId = catId;
            item.target = url.toString();
            item.name = url.host().isEmpty() ? item.target : url.host();
            batch.append(item);
        }
    }
    if (batch.isEmpty()) {
        setStatus(QStringLiteral("未识别到可添加的文件、快捷方式或网址"), true);
        return 0;
    }
    const int added = m_store->addItems(batch);
    if (added > 0) {
        setStatus(QStringLiteral("已添加 %1 项").arg(added), false);
    } else {
        setStatus(QStringLiteral("拖入的项目已存在"), false);
    }
    return added;
}

QString AppController::pickFile(const QString& title, const QString& filterName, const QString& filterPattern)
{
    m_pickingFile = true;
    const QString result = ShellBridge::pickOpenFile(
        m_window.data(),
        title.isEmpty() ? QStringLiteral("选择文件") : title,
        filterName.isEmpty() ? QStringLiteral("所有文件 (*.*)") : filterName,
        filterPattern.isEmpty() ? QStringLiteral("*.*") : filterPattern);
    m_pickingFile = false;
    if (m_window && m_window->isVisible()) {
        ShellBridge::activateWindow(m_window.data());
    }
    return result;
}

QString AppController::pickFolder(const QString& title)
{
    m_pickingFile = true;
    const QString result = ShellBridge::pickFolder(
        m_window.data(),
        title.isEmpty() ? QStringLiteral("选择文件夹") : title);
    m_pickingFile = false;
    if (m_window && m_window->isVisible()) {
        ShellBridge::activateWindow(m_window.data());
    }
    return result;
}

void AppController::clearStatus()
{
    if (m_statusMessage.isEmpty() && !m_statusError) {
        return;
    }
    m_statusMessage.clear();
    m_statusError = false;
    emit statusChanged();
}

void AppController::setStatus(const QString& message, bool isError)
{
    if (m_statusMessage == message && m_statusError == isError) {
        return;
    }
    m_statusMessage = message;
    m_statusError = isError;
    emit statusChanged();
}
QJsonObject AppController::settingsToJson() const
{
    return QJsonObject{
        {QStringLiteral("popupHotkey"), m_settings.popupHotkey},
        {QStringLiteral("hideOnBlur"), m_settings.hideOnBlur},
        {QStringLiteral("hideAfterLaunch"), m_settings.hideAfterLaunch},
        {QStringLiteral("singleClickLaunch"), m_settings.singleClickLaunch},
        {QStringLiteral("showAtCursor"), m_settings.showAtCursor},
        {QStringLiteral("topMost"), m_settings.topMost},
        {QStringLiteral("viewMode"), m_settings.viewMode},
        {QStringLiteral("tileSize"), m_settings.tileSize},
    };
}

void AppController::pushSettingsUpdate()
{
    if (m_protocol) {
        const QJsonObject hostAllowed{
            {QStringLiteral("hideOnBlur"), m_settings.hideOnBlur},
            {QStringLiteral("hideAfterLaunch"), m_settings.hideAfterLaunch},
            {QStringLiteral("singleClickLaunch"), m_settings.singleClickLaunch},
            {QStringLiteral("showAtCursor"), m_settings.showAtCursor},
            {QStringLiteral("topMost"), m_settings.topMost},
            {QStringLiteral("viewMode"), m_settings.viewMode},
            {QStringLiteral("tileSize"), m_settings.tileSize},
        };
        m_protocol->sendSettingsUpdate(hostAllowed);
    } else if (m_standaloneMode) {
        saveStandaloneSettings();
    }
}

void AppController::loadStandaloneSettings()
{
    const QString path = QDir(m_dataDir).filePath(QStringLiteral("standalone-settings.json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) {
        m_settings = PluginSettings::fromJson(doc.object());
    }
}

void AppController::saveStandaloneSettings() const
{
    if (m_dataDir.isEmpty()) {
        return;
    }
    QDir().mkpath(m_dataDir);
    QSaveFile f(QDir(m_dataDir).filePath(QStringLiteral("standalone-settings.json")));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(settingsToJson()).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void AppController::applySettings(const QVariantMap& map)
{
    if (map.contains(QStringLiteral("popupHotkey"))) {
        m_settings.popupHotkey = map.value(QStringLiteral("popupHotkey")).toString().trimmed();
    }
    if (map.contains(QStringLiteral("hideOnBlur"))) {
        m_settings.hideOnBlur = map.value(QStringLiteral("hideOnBlur")).toBool();
    }
    if (map.contains(QStringLiteral("hideAfterLaunch"))) {
        m_settings.hideAfterLaunch = map.value(QStringLiteral("hideAfterLaunch")).toBool();
    }
    if (map.contains(QStringLiteral("singleClickLaunch"))) {
        m_settings.singleClickLaunch = map.value(QStringLiteral("singleClickLaunch")).toBool();
    }
    if (map.contains(QStringLiteral("showAtCursor"))) {
        m_settings.showAtCursor = map.value(QStringLiteral("showAtCursor")).toBool();
    }
    if (map.contains(QStringLiteral("topMost"))) {
        m_settings.topMost = map.value(QStringLiteral("topMost")).toBool();
    }
    if (map.contains(QStringLiteral("viewMode"))) {
        const QString vm = map.value(QStringLiteral("viewMode")).toString();
        m_settings.viewMode = (vm == QLatin1String("list")) ? QStringLiteral("list") : QStringLiteral("grid");
    }
    if (map.contains(QStringLiteral("tileSize"))) {
        const QString ts = map.value(QStringLiteral("tileSize")).toString();
        m_settings.tileSize = (ts == QLatin1String("small") || ts == QLatin1String("large")) ? ts : QStringLiteral("medium");
    }
    m_hotkeysPaused = false;
    if (m_hotkeyService) {
        m_hotkeyService->applyHotkey(m_settings.popupHotkey);
    }
    emit settingsChanged();
    pushSettingsUpdate();
    if (m_window) {
        ShellBridge::applyWindowChrome(m_window.data(), m_theme->data());
    }
    setStatus(QStringLiteral("设置已保存"), false);
}
QString AppController::defaultMayePath() const
{
    QStringList candidates = {
        QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("MayeNano/MDB.json")),
        QDir(qEnvironmentVariable("APPDATA")).filePath(QStringLiteral("MayeNano/MDB.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("MayeNano/MDB.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("MayeNano/MDB.json")),
        QDir(QDir::homePath()).filePath(QStringLiteral("MayeNano/MDB.json")),
    };
    const QFileInfoList drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        const QString root = drive.absoluteFilePath();
        candidates.append(QDir(root).filePath(QStringLiteral("software/MayeNano/MDB.json")));
        candidates.append(QDir(root).filePath(QStringLiteral("MayeNano/MDB.json")));
        candidates.append(QDir(root).filePath(QStringLiteral("Program Files/MayeNano/MDB.json")));
    }
    for (const QString& p : candidates) {
        if (!p.isEmpty() && QFileInfo::exists(p)) {
            return QDir::toNativeSeparators(p);
        }
    }
    return {};
}

bool AppController::hasDefaultMayeFile() const
{
    return !defaultMayePath().isEmpty();
}

bool AppController::importMayeDefault()
{
    if (!m_store) {
        return false;
    }
    const QString path = defaultMayePath();
    if (path.isEmpty()) {
        return importMayeFromDialog();
    }
    const MayeImportResult res = MayeNanoImporter::importFromFile(path, *m_store);
    const QString summary = QStringLiteral("MayeNano 导入完成：新增分组 %1 个，新增条目 %2 条，跳过重复 %3 条")
        .arg(res.addedCategories)
        .arg(res.addedItems)
        .arg(res.skippedDuplicates);
    const bool hasWarn = !res.warnings.isEmpty() && res.addedItems == 0 && res.addedCategories == 0;
    setStatus(hasWarn ? (summary + QStringLiteral(" · ") + res.warnings.first()) : summary, hasWarn);
    return !hasWarn;
}

bool AppController::importMayeFromDialog()
{
    if (!m_store) {
        return false;
    }
    const QString path = pickFile(
        QStringLiteral("选择 MayeNano 数据库 (MDB.json)"),
        QStringLiteral("JSON 文件 (*.json)"),
        QStringLiteral("*.json"));
    if (path.isEmpty()) {
        return false;
    }
    const MayeImportResult res = MayeNanoImporter::importFromFile(path, *m_store);
    const QString summary = QStringLiteral("导入完成：新增分组 %1 个，新增条目 %2 条，跳过重复 %3 条")
        .arg(res.addedCategories)
        .arg(res.addedItems)
        .arg(res.skippedDuplicates);
    const bool hasWarn = !res.warnings.isEmpty() && res.addedItems == 0 && res.addedCategories == 0;
    setStatus(hasWarn ? (summary + QStringLiteral(" · ") + res.warnings.first()) : summary, hasWarn);
    return !hasWarn;
}

bool AppController::exportBackupDialog()
{
    if (!m_store) {
        return false;
    }
    m_pickingFile = true;
    const QString path = ShellBridge::pickSaveFile(
        m_window.data(),
        QStringLiteral("导出快速启动面板备份"),
        QStringLiteral("JSON 备份 (*.json)"),
        QStringLiteral("*.json"),
        QStringLiteral("quicklaunch-backup.json"));
    m_pickingFile = false;
    if (m_window && m_window->isVisible()) {
        ShellBridge::activateWindow(m_window.data());
    }
    if (path.isEmpty()) {
        return false;
    }
    QString err;
    if (!m_store->exportJson(path, &err)) {
        setStatus(err.isEmpty() ? QStringLiteral("导出备份失败") : err, true);
        return false;
    }
    setStatus(QStringLiteral("已导出备份至：%1").arg(path), false);
    return true;
}
bool AppController::importBackupDialog()
{
    if (!m_store) {
        return false;
    }
    const QString path = pickFile(
        QStringLiteral("导入快速启动面板备份"),
        QStringLiteral("JSON 备份 (*.json)"),
        QStringLiteral("*.json"));
    if (path.isEmpty()) {
        return false;
    }
    QString msg;
    if (!m_store->importBackupJson(path, &msg)) {
        setStatus(msg.isEmpty() ? QStringLiteral("导入备份失败") : msg, true);
        return false;
    }
    setStatus(msg.isEmpty() ? QStringLiteral("导入备份成功") : msg, false);
    return true;
}

void AppController::openDataDir()
{
    if (m_dataDir.trimmed().isEmpty()) {
        setStatus(QStringLiteral("数据目录尚未初始化"), true);
        return;
    }
    QDir().mkpath(m_dataDir);
    const QString nativeDir = QDir::toNativeSeparators(m_dataDir);
    if (m_protocol) {
        m_protocol->tool(QStringLiteral("open_item"),
            QJsonObject{{QStringLiteral("path"), nativeDir}},
            [this](const QJsonValue&, const QString& err) {
                if (!err.isEmpty()) {
                    setStatus(QStringLiteral("打开数据目录失败：%1").arg(err), true);
                }
            });
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(nativeDir))) {
        setStatus(QStringLiteral("打开数据目录失败"), true);
    }
}

QJsonObject AppController::buildQueryItem(const LaunchItem& item, const QString& categoryName, int score) const
{
    const QString expanded = ShellBridge::expandPath(item.target);
    QString icon;
    QString copyText;
    if (ShellBridge::isWebUrl(expanded)) {
        icon = QStringLiteral("site:") + expanded;
        copyText = expanded;
    } else if (ShellBridge::isSpecialSystemUri(expanded)) {
        icon = (expanded.compare(QLatin1String("$controls"), Qt::CaseInsensitive) == 0)
            ? QStringLiteral("ph:sliders")
            : QStringLiteral("ph:monitor");
        copyText = expanded;
    } else {
        const QString full = ShellBridge::resolveFullPath(expanded);
        const QString native = QDir::toNativeSeparators(full.isEmpty() ? expanded : full);
        icon = QStringLiteral("file:") + native;
        copyText = native;
    }

    QString subtitle = categoryName.isEmpty()
        ? copyText
        : QStringLiteral("[%1] %2").arg(categoryName, copyText);
    if (!item.remark.trimmed().isEmpty()) {
        subtitle += QStringLiteral(" · ") + item.remark.trimmed();
    }

    const QJsonArray actions{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("launch")}, {QStringLiteral("title"), QStringLiteral("启动")}, {QStringLiteral("key"), QStringLiteral("Enter")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("reveal")}, {QStringLiteral("title"), QStringLiteral("打开所在文件夹")}, {QStringLiteral("key"), QStringLiteral("Ctrl+Enter")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("open-panel")}, {QStringLiteral("title"), QStringLiteral("打开快捷面板")}, {QStringLiteral("key"), QStringLiteral("Shift+Enter")}},
    };

    return QJsonObject{
        {QStringLiteral("id"), item.id},
        {QStringLiteral("title"), item.name},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("score"), qBound(1, score, 100)},
        {QStringLiteral("copy"), copyText},
        {QStringLiteral("actions"), actions},
    };
}
void AppController::handleMessage(const QJsonValue& id, const QString& method, const QJsonObject& params, bool hasId)
{
    if (method == QLatin1String("initialize")) {
        handleInitialize(id, params, hasId);
    } else if (method == QLatin1String("query")) {
        handleQuery(id, params, hasId);
    } else if (method == QLatin1String("action")) {
        handleAction(id, params, hasId);
    } else if (method == QLatin1String("trayMenu")) {
        handleTrayMenu(id, hasId);
    } else if (method == QLatin1String("menu")) {
        handleMenu(params);
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("showSettings")) {
        showSettingsDialog();
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("settingsChanged")) {
        handleSettingsChanged(params);
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("appearance")) {
        handleAppearance(params);
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("hotkey")) {
        handleHotkey(params);
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("cancel")) {
        if (hasId && m_protocol) m_protocol->respond(id, QJsonObject{});
    } else if (method == QLatin1String("shutdown")) {
        handleShutdown(id, hasId);
    } else if (hasId && m_protocol) {
        m_protocol->respondError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
    }
}

void AppController::handleInitialize(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    m_pluginDir = params.value(QStringLiteral("pluginDir")).toString();
    const QString dataDir = params.value(QStringLiteral("dataDir")).toString();
    const QString locale = params.value(QStringLiteral("locale")).toString(QStringLiteral("zh"));
    const QString themeStr = params.value(QStringLiteral("theme")).toString(QStringLiteral("dark"));
    const QJsonObject appearance = params.value(QStringLiteral("appearance")).toObject();
    const QJsonObject settingsObj = params.value(QStringLiteral("settings")).toObject();
    const bool hostHotkeys = params.value(QStringLiteral("capabilities")).toObject()
        .value(QStringLiteral("hostHotkeys")).toBool(false);

    m_theme->update(ThemeData::fromAppearance(locale, themeStr, appearance));
    m_settings = PluginSettings::fromJson(settingsObj);

    initializeStorage(dataDir.isEmpty() ? m_dataDir : dataDir, hostHotkeys);
    emit settingsChanged();

    if (hasId && m_protocol) {
        m_protocol->respond(id, QJsonObject{
            {QStringLiteral("protocolVersion"), QStringLiteral("1")},
            {QStringLiteral("name"), QStringLiteral("快速启动面板")},
        });
    }

    QTimer::singleShot(350, this, [this]() {
        if (!m_window && m_initialized) {
            ensureWindow();
        }
    });
}

void AppController::handleQuery(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    if (!hasId || !m_protocol) {
        return;
    }
    const QString text = params.value(QStringLiteral("text")).toString().trimmed();
    const int limit = qBound(1, params.value(QStringLiteral("limit")).toInt(20), 200);
    QJsonArray items;
    if (text.isEmpty()) {
        const QString hk = m_settings.popupHotkey.trimmed();
        const QString openTitle = hk.isEmpty()
            ? QStringLiteral("打开快速启动面板")
            : QStringLiteral("打开快速启动面板 (%1)").arg(hk);
        items.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("__open_panel__")},
            {QStringLiteral("title"), openTitle},
            {QStringLiteral("subtitle"), QStringLiteral("呼出快速启动面板，支持分组、拖放添加与导入 MayeNano")},
            {QStringLiteral("icon"), QStringLiteral("ph:rocket-launch")},
            {QStringLiteral("score"), 100},
            {QStringLiteral("actions"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("open-panel")},
                    {QStringLiteral("title"), QStringLiteral("打开快捷面板")},
                    {QStringLiteral("key"), QStringLiteral("Enter")},
                }
            }},
        });

        if (m_store) {
            const LauncherDatabase& db = m_cachedDb;
            QHash<QString, QString> catNames;
            for (const auto& c : db.categories) {
                catNames.insert(c.id, c.name);
            }
            QList<LaunchItem> sorted = db.items;
            std::stable_sort(sorted.begin(), sorted.end(), [](const LaunchItem& a, const LaunchItem& b) {
                if (a.runCount != b.runCount) {
                    return a.runCount > b.runCount;
                }
                return a.order < b.order;
            });
            const int maxFrequent = qMin(12, qMax(0, limit - 1));
            for (int i = 0; i < sorted.size() && i < maxFrequent; ++i) {
                const LaunchItem& it = sorted.at(i);
                items.append(buildQueryItem(it, catNames.value(it.categoryId), 90 - i));
            }
        }
    } else if (m_store) {
        const QList<SearchMatch> matches = PinyinMatcher::search(m_searchIndex, text, m_activeCategoryId, true);
        if (matches.isEmpty()) {
            items.append(QJsonObject{
                {QStringLiteral("id"), QStringLiteral("__no_match__")},
                {QStringLiteral("title"), QStringLiteral("未找到与“%1”匹配的启动项").arg(text)},
                {QStringLiteral("subtitle"), QStringLiteral("可换个关键词或拼音首字母，或按快捷键打开面板添加")},
                {QStringLiteral("icon"), QStringLiteral("ph:magnifying-glass")},
                {QStringLiteral("valid"), false},
            });
        } else {
            const int cap = qMin(limit, 20);
            for (int i = 0; i < matches.size() && i < cap; ++i) {
                const SearchMatch& m = matches.at(i);
                items.append(buildQueryItem(m.item, m.categoryName, m.score));
            }
        }
    }

    m_protocol->respond(id, QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("rerun"), 0},
        {QStringLiteral("cache"), 0},
    });
}
void AppController::handleAction(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    auto replyEffects = [&](const QJsonArray& effects) {
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{{QStringLiteral("effects"), effects}});
        }
    };

    const QString trigger = params.value(QStringLiteral("trigger")).toString();
    const QString act = params.value(QStringLiteral("action")).toString();
    const QJsonObject itemObj = params.value(QStringLiteral("item")).toObject();
    const QString itemId = itemObj.value(QStringLiteral("id")).toString();

    if (trigger == QLatin1String("files") || act == QLatin1String("files")) {
        if (!m_store) {
            replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("存储尚未初始化")}}});
            return;
        }
        const QString targetCat = resolveTargetCategoryForInsert(QString());
        const QJsonArray files = params.value(QStringLiteral("files")).toArray();
        QList<LaunchItem> batch;
        batch.reserve(files.size());
        for (const QJsonValue& v : files) {
            const QString path = v.toString().trimmed();
            if (!path.isEmpty()) {
                batch.append(makeItemFromLocalPath(path, targetCat));
            }
        }
        const int added = m_store->addItems(batch);
        const QString msg = added > 0
            ? QStringLiteral("已添加 %1 项到快速启动面板").arg(added)
            : QStringLiteral("所选项目已在快速启动面板中");
        replyEffects({
            QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), msg}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}},
        });
        return;
    }

    if (act == QLatin1String("open-panel") || itemId == QLatin1String("__open_panel__")) {
        QMetaObject::invokeMethod(this, [this]() { showPopup(); }, Qt::QueuedConnection);
        replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}}});
        return;
    }

    if (!m_store) {
        replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("存储尚未初始化")}}});
        return;
    }
    const std::optional<LaunchItem> found = m_store->findById(itemId);
    if (!found) {
        replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("未找到该启动项")}}});
        return;
    }

    const QString expanded = ShellBridge::expandPath(found->target);
    if (act == QLatin1String("reveal")) {
        if (ShellBridge::isWebUrl(expanded) || ShellBridge::isSpecialSystemUri(expanded)) {
            replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("网址或系统快捷项没有本地文件夹")}}});
            return;
        }
        const QString resolved = ShellBridge::resolveFullPath(expanded);
        if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
            replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("找不到目标路径：%1").arg(found->target)}}});
            return;
        }
        replyEffects({
            QJsonObject{{QStringLiteral("type"), QStringLiteral("reveal")}, {QStringLiteral("path"), QDir::toNativeSeparators(resolved)}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}},
        });
        return;
    }
    if (!ShellBridge::existsOrResolvable(expanded)) {
        replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), QStringLiteral("目标不存在：%1").arg(found->target)}}});
        return;
    }

    if (ShellBridge::isWebUrl(expanded)) {
        m_store->recordLaunch(found->id);
        replyEffects({
            QJsonObject{{QStringLiteral("type"), QStringLiteral("open")}, {QStringLiteral("url"), expanded}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}},
        });
        return;
    }

    QString launchPath;
    if (ShellBridge::needsShimLnk(*found, false)) {
        QString err;
        launchPath = ShellBridge::ensureLaunchLnk(m_dataDir, *found, false, &err);
        if (launchPath.isEmpty()) {
            replyEffects({QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), err.isEmpty() ? QStringLiteral("创建启动快捷方式失败") : err}}});
            return;
        }
    } else {
        launchPath = ShellBridge::resolveFullPath(expanded);
        if (launchPath.isEmpty()) {
            launchPath = expanded;
        }
    }

    m_store->recordLaunch(found->id);
    replyEffects({
        QJsonObject{{QStringLiteral("type"), QStringLiteral("open")}, {QStringLiteral("path"), QDir::toNativeSeparators(launchPath)}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}},
    });
}

void AppController::handleTrayMenu(const QJsonValue& id, bool hasId)
{
    if (!hasId || !m_protocol) {
        return;
    }
    const QString hk = m_settings.popupHotkey.trimmed();
    const QString openTitle = hk.isEmpty()
        ? QStringLiteral("打开面板")
        : QStringLiteral("打开面板\t%1").arg(hk);
    const QJsonArray items{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("open-panel")}, {QStringLiteral("title"), openTitle}},
        QJsonObject{},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("settings")}, {QStringLiteral("title"), QStringLiteral("设置...")}},
    };
    m_protocol->respond(id, QJsonObject{{QStringLiteral("items"), items}});
}

void AppController::handleMenu(const QJsonObject& params)
{
    const QString menuId = params.value(QStringLiteral("id")).toString();
    if (menuId == QLatin1String("open-panel")) {
        showPopup();
    } else if (menuId == QLatin1String("settings")) {
        showSettingsDialog();
    }
}
void AppController::handleSettingsChanged(const QJsonObject& params)
{
    const QJsonObject obj = params.contains(QStringLiteral("settings"))
        ? params.value(QStringLiteral("settings")).toObject()
        : params;
    m_settings = PluginSettings::fromJson(obj);
    if (m_hotkeyService && !m_hotkeysPaused) {
        m_hotkeyService->applyHotkey(m_settings.popupHotkey);
    }
    emit settingsChanged();
    if (m_window) {
        ShellBridge::applyWindowChrome(m_window.data(), m_theme->data());
    }
}

void AppController::handleAppearance(const QJsonObject& params)
{
    const QString locale = params.value(QStringLiteral("locale")).toString();
    const QString themeStr = params.value(QStringLiteral("theme")).toString();
    m_theme->update(ThemeData::fromAppearance(locale, themeStr, params));
    if (m_window) {
        ShellBridge::applyWindowChrome(m_window.data(), m_theme->data());
    }
}

void AppController::handleHotkey(const QJsonObject& params)
{
    if (m_hotkeyService) {
        m_hotkeyService->handleHostHotkey(params.value(QStringLiteral("key")).toString());
    }
}

void AppController::handleShutdown(const QJsonValue& id, bool hasId)
{
    if (m_hotkeyService) {
        m_hotkeyService->releaseAll();
    }
    if (m_window) {
        m_window->hide();
        m_window->close();
    }
    if (hasId && m_protocol) {
        m_protocol->respond(id, QJsonObject{});
    }
    QCoreApplication::quit();
}

} // namespace ql
