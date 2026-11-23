#include "LauncherStore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>
#include <QtGlobal>
#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace ql {
namespace {

bool hasCategory(const QList<CategoryEntry>& cats, const QString& id) {
    return std::any_of(cats.cbegin(), cats.cend(),
                       [&](const CategoryEntry& c) { return c.id == id; });
}

bool hasItemId(const QList<LaunchItem>& items, const QString& id) {
    return std::any_of(items.cbegin(), items.cend(),
                       [&](const LaunchItem& it) { return it.id == id; });
}

int nextCategoryOrder(const QList<CategoryEntry>& cats) {
    int maxOrd = -1;
    for (const auto& c : cats) {
        maxOrd = std::max(maxOrd, c.order);
    }
    return maxOrd + 1;
}

int nextItemOrder(const QList<LaunchItem>& items) {
    int maxOrd = -1;
    for (const auto& it : items) {
        maxOrd = std::max(maxOrd, it.order);
    }
    return maxOrd + 1;
}

using ItemDedupKey = std::tuple<QString, QString, QString>;

ItemDedupKey makeDedupKey(const LaunchItem& item) {
    return std::make_tuple(
        item.categoryId.trimmed().toLower(),
        item.target.trimmed().toLower(),
        item.arguments.trimmed().toLower());
}

bool writeJsonAtomically(const QString& destPath, const QJsonObject& obj, QString* error) {
    const QString parentDir = QFileInfo(destPath).absolutePath();
    if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
        if (error) {
            *error = QStringLiteral("无法创建目标目录: %1").arg(parentDir);
        }
        return false;
    }
    QSaveFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法打开目标文件: %1").arg(file.errorString());
        }
        return false;
    }
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        if (error) {
            *error = QStringLiteral("写入文件失败: %1").arg(file.errorString());
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("提交文件失败: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace

LauncherStore::LauncherStore(QString dataDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(std::move(dataDir)) {
}

QString LauncherStore::dataDir() const {
    return m_dataDir;
}

QString LauncherStore::dbPath() const {
    return QDir(m_dataDir).filePath(QStringLiteral("launcher-db.json"));
}

LauncherDatabase LauncherStore::makeDefaultDatabase() {
    LauncherDatabase db;
    db.version = 1;
    db.activeCategoryId = QStringLiteral("default");
    db.windowWidth = 760.0;
    db.windowHeight = 520.0;
    db.categories.append(CategoryEntry{
        QStringLiteral("default"),
        QStringLiteral("常用"),
        0
    });

    auto makeItem = [](const QString& id, const QString& name,
                       const QString& target, const QString& kw, int order) {
        LaunchItem it;
        it.id = id;
        it.categoryId = QStringLiteral("default");
        it.name = name;
        it.target = target;
        it.keywords = kw;
        it.order = order;
        return it;
    };

    db.items.append(makeItem(
        QStringLiteral("def_computer"), QStringLiteral("此电脑"),
        QStringLiteral("$computer"), QString(), 0));
    db.items.append(makeItem(
        QStringLiteral("def_controls"), QStringLiteral("控制面板"),
        QStringLiteral("$controls"), QString(), 1));
    db.items.append(makeItem(
        QStringLiteral("def_notepad"), QStringLiteral("记事本"),
        QStringLiteral("notepad.exe"),
        QStringLiteral("notepad jishiben jsb"), 2));
    db.items.append(makeItem(
        QStringLiteral("def_cmd"), QStringLiteral("命令提示符"),
        QStringLiteral("cmd.exe"),
        QStringLiteral("cmd terminal 命令行"), 3));
    return db;
}

void LauncherStore::sortDatabase(LauncherDatabase& db) {
    std::stable_sort(db.categories.begin(), db.categories.end(),
                     [](const CategoryEntry& a, const CategoryEntry& b) {
                         return a.order < b.order;
                     });
    std::stable_sort(db.items.begin(), db.items.end(),
                     [](const LaunchItem& a, const LaunchItem& b) {
                         return a.order < b.order;
                     });
}

QString LauncherStore::generateId() {
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

void LauncherStore::normalizeDatabaseLocked() {
    if (m_db.categories.isEmpty()) {
        m_db.categories.append(CategoryEntry{
            QStringLiteral("default"),
            QStringLiteral("常用"),
            0
        });
    }
    for (auto& cat : m_db.categories) {
        if (cat.id.trimmed().isEmpty()) {
            cat.id = generateId();
        }
        if (cat.name.trimmed().isEmpty()) {
            cat.name = QStringLiteral("未命名");
        }
    }
    if (m_db.windowWidth <= 0.0) {
        m_db.windowWidth = 760.0;
    }
    if (m_db.windowHeight <= 0.0) {
        m_db.windowHeight = 520.0;
    }
    sortDatabase(m_db);
    const QString firstCatId = m_db.categories.first().id;
    for (auto& it : m_db.items) {
        if (it.id.trimmed().isEmpty()) {
            it.id = generateId();
        }
        if (it.categoryId.trimmed().isEmpty() || !hasCategory(m_db.categories, it.categoryId)) {
            it.categoryId = firstCatId;
        }
    }
    if (m_db.activeCategoryId != QLatin1String("__all__") &&
        !hasCategory(m_db.categories, m_db.activeCategoryId)) {
        m_db.activeCategoryId = firstCatId;
    }
}

bool LauncherStore::saveLocked() const {
    QString err;
    if (!writeJsonAtomically(dbPath(), m_db.toJson(), &err)) {
        qWarning() << "LauncherStore:" << err << dbPath();
        return false;
    }
    return true;
}

bool LauncherStore::backupCorruptFile(const QString& sourcePath) const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QDir dir(m_dataDir);
    QString bakPath = dir.filePath(QStringLiteral("launcher-db.corrupt.%1.bak").arg(stamp));
    int suffix = 1;
    while (QFile::exists(bakPath)) {
        bakPath = dir.filePath(QStringLiteral("launcher-db.corrupt.%1.%2.bak").arg(stamp).arg(suffix++));
    }
    if (!QFile::copy(sourcePath, bakPath)) {
        qWarning() << "LauncherStore: failed to backup corrupt db to" << bakPath;
        return false;
    }
    return true;
}

LauncherDatabase LauncherStore::loadOrInitialize() {
    QMutexLocker locker(&m_mutex);
    const QString path = dbPath();
    QDir().mkpath(m_dataDir);

    if (!QFile::exists(path)) {
        m_db = makeDefaultDatabase();
        saveLocked();
        return m_db;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "LauncherStore: failed to open database:" << path << file.errorString();
        backupCorruptFile(path);
        m_db = makeDefaultDatabase();
        saveLocked();
        return m_db;
    }

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "LauncherStore: corrupt database JSON:" << path << err.errorString();
        backupCorruptFile(path);
        m_db = makeDefaultDatabase();
        saveLocked();
        return m_db;
    }

    m_db = LauncherDatabase::fromJson(doc.object());
    normalizeDatabaseLocked();
    return m_db;
}

LauncherDatabase LauncherStore::snapshot() const {
    QMutexLocker locker(&m_mutex);
    LauncherDatabase copy = m_db;
    sortDatabase(copy);
    return copy;
}

std::optional<LaunchItem> LauncherStore::findById(const QString& itemId) const {
    QMutexLocker locker(&m_mutex);
    for (const auto& it : m_db.items) {
        if (it.id == itemId) {
            return it;
        }
    }
    return std::nullopt;
}

CategoryEntry LauncherStore::addCategory(const QString& name) {
    CategoryEntry created;
    {
        QMutexLocker locker(&m_mutex);
        created.name = name.trimmed();
        if (created.name.isEmpty()) {
            created.name = QStringLiteral("未命名");
        }
        do {
            created.id = generateId();
        } while (hasCategory(m_db.categories, created.id));
        created.order = nextCategoryOrder(m_db.categories);
        m_db.categories.append(created);
        sortDatabase(m_db);
        if (m_db.activeCategoryId.isEmpty()) {
            m_db.activeCategoryId = m_db.categories.first().id;
        }
        saveLocked();
    }
    emit changed();
    return created;
}

bool LauncherStore::renameCategory(const QString& id, const QString& newName) {
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    bool modified = false;
    {
        QMutexLocker locker(&m_mutex);
        for (auto& cat : m_db.categories) {
            if (cat.id == id) {
                if (cat.name == trimmed) {
                    return true;
                }
                cat.name = trimmed;
                modified = true;
                saveLocked();
                break;
            }
        }
    }
    if (modified) {
        emit changed();
    }
    return modified;
}

bool LauncherStore::deleteCategory(const QString& id, bool moveItemsToDefault) {
    {
        QMutexLocker locker(&m_mutex);
        if (m_db.categories.size() <= 1) {
            return false;
        }
        qsizetype idx = -1;
        for (qsizetype i = 0; i < m_db.categories.size(); ++i) {
            if (m_db.categories[i].id == id) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            return false;
        }
        m_db.categories.removeAt(idx);
        sortDatabase(m_db);
        for (int i = 0; i < m_db.categories.size(); ++i) {
            m_db.categories[i].order = i;
        }

        QString fallbackId = m_db.categories.first().id;
        for (const auto& cat : m_db.categories) {
            if (cat.id == QLatin1String("default")) {
                fallbackId = cat.id;
                break;
            }
        }
        if (moveItemsToDefault) {
            int nextOrd = nextItemOrder(m_db.items);
            for (auto& it : m_db.items) {
                if (it.categoryId == id) {
                    it.categoryId = fallbackId;
                    it.order = nextOrd++;
                }
            }
        } else {
            m_db.items.erase(
                std::remove_if(m_db.items.begin(), m_db.items.end(),
                               [&](const LaunchItem& it) { return it.categoryId == id; }),
                m_db.items.end());
        }
        if (m_db.activeCategoryId == id) {
            m_db.activeCategoryId = m_db.categories.first().id;
        }
        sortDatabase(m_db);
        saveLocked();
    }
    emit changed();
    return true;
}

bool LauncherStore::moveCategoryOrder(const QString& id, int delta) {
    if (delta == 0) {
        return false;
    }
    {
        QMutexLocker locker(&m_mutex);
        sortDatabase(m_db);
        qsizetype idx = -1;
        for (qsizetype i = 0; i < m_db.categories.size(); ++i) {
            if (m_db.categories[i].id == id) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            return false;
        }
        const qsizetype target = std::clamp<qsizetype>(
            idx + delta, 0, m_db.categories.size() - 1);
        if (target == idx) {
            return false;
        }
        m_db.categories.move(idx, target);
        for (int i = 0; i < m_db.categories.size(); ++i) {
            m_db.categories[i].order = i;
        }
        saveLocked();
    }
    emit changed();
    return true;
}

void LauncherStore::setActiveCategory(const QString& categoryId) {
    bool modified = false;
    {
        QMutexLocker locker(&m_mutex);
        const bool valid = categoryId.isEmpty() ||
                           categoryId == QLatin1String("__all__") ||
                           hasCategory(m_db.categories, categoryId);
        if (!valid || m_db.activeCategoryId == categoryId) {
            return;
        }
        m_db.activeCategoryId = categoryId;
        saveLocked();
        modified = true;
    }
    if (modified) {
        emit changed();
    }
}

LaunchItem LauncherStore::addItem(LaunchItem item) {
    {
        QMutexLocker locker(&m_mutex);
        if (m_db.categories.isEmpty()) {
            m_db.categories.append(CategoryEntry{
                QStringLiteral("default"), QStringLiteral("常用"), 0
            });
        }
        sortDatabase(m_db);
        if (item.id.trimmed().isEmpty() || hasItemId(m_db.items, item.id)) {
            do {
                item.id = generateId();
            } while (hasItemId(m_db.items, item.id));
        }
        if (item.categoryId.trimmed().isEmpty() ||
            !hasCategory(m_db.categories, item.categoryId)) {
            item.categoryId = m_db.categories.first().id;
        }
        item.order = nextItemOrder(m_db.items);
        m_db.items.append(item);
        sortDatabase(m_db);
        saveLocked();
    }
    emit changed();
    return item;
}

int LauncherStore::addItems(const QList<LaunchItem>& items) {
    if (items.isEmpty()) {
        return 0;
    }
    int added = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (m_db.categories.isEmpty()) {
            m_db.categories.append(CategoryEntry{
                QStringLiteral("default"), QStringLiteral("常用"), 0
            });
        }
        sortDatabase(m_db);
        std::set<ItemDedupKey> seen;
        for (const auto& existing : m_db.items) {
            seen.insert(makeDedupKey(existing));
        }
        int nextOrd = nextItemOrder(m_db.items);
        const QString firstCatId = m_db.categories.first().id;
        for (const auto& raw : items) {
            LaunchItem copy = raw;
            if (copy.categoryId.trimmed().isEmpty() ||
                !hasCategory(m_db.categories, copy.categoryId)) {
                copy.categoryId = firstCatId;
            }
            if (!seen.insert(makeDedupKey(copy)).second) {
                continue;
            }
            if (copy.id.trimmed().isEmpty() || hasItemId(m_db.items, copy.id)) {
                do {
                    copy.id = generateId();
                } while (hasItemId(m_db.items, copy.id));
            }
            copy.order = nextOrd++;
            m_db.items.append(std::move(copy));
            ++added;
        }
        if (added == 0) {
            return 0;
        }
        sortDatabase(m_db);
        saveLocked();
    }
    emit changed();
    return added;
}

bool LauncherStore::updateItem(const LaunchItem& updated) {
    if (updated.id.trimmed().isEmpty()) {
        return false;
    }
    bool found = false;
    {
        QMutexLocker locker(&m_mutex);
        for (auto& it : m_db.items) {
            if (it.id == updated.id) {
                LaunchItem next = updated;
                if (next.categoryId.trimmed().isEmpty() ||
                    !hasCategory(m_db.categories, next.categoryId)) {
                    next.categoryId = it.categoryId;
                }
                it = std::move(next);
                sortDatabase(m_db);
                saveLocked();
                found = true;
                break;
            }
        }
    }
    if (found) {
        emit changed();
    }
    return found;
}

bool LauncherStore::deleteItem(const QString& itemId) {
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        const qsizetype before = m_db.items.size();
        m_db.items.erase(
            std::remove_if(m_db.items.begin(), m_db.items.end(),
                           [&](const LaunchItem& it) { return it.id == itemId; }),
            m_db.items.end());
        if (m_db.items.size() != before) {
            removed = true;
            saveLocked();
        }
    }
    if (removed) {
        emit changed();
    }
    return removed;
}

bool LauncherStore::moveItemToCategory(const QString& itemId, const QString& targetCategoryId) {
    bool moved = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!hasCategory(m_db.categories, targetCategoryId)) {
            return false;
        }
        for (auto& it : m_db.items) {
            if (it.id == itemId) {
                if (it.categoryId == targetCategoryId) {
                    return true;
                }
                it.categoryId = targetCategoryId;
                it.order = nextItemOrder(m_db.items);
                sortDatabase(m_db);
                saveLocked();
                moved = true;
                break;
            }
        }
    }
    if (moved) {
        emit changed();
    }
    return moved;
}

bool LauncherStore::moveItemOrder(const QString& itemId, int delta) {
    if (delta == 0) {
        return false;
    }
    {
        QMutexLocker locker(&m_mutex);
        sortDatabase(m_db);
        bool found = false;
        QString catId;
        for (const auto& it : m_db.items) {
            if (it.id == itemId) {
                catId = it.categoryId;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }

        QList<qsizetype> catIndices;
        qsizetype pos = -1;
        for (qsizetype i = 0; i < m_db.items.size(); ++i) {
            if (m_db.items[i].categoryId == catId) {
                if (m_db.items[i].id == itemId) {
                    pos = catIndices.size();
                }
                catIndices.append(i);
            }
        }
        const qsizetype targetPos = std::clamp<qsizetype>(
            pos + delta, 0, catIndices.size() - 1);
        if (pos < 0 || targetPos == pos) {
            return false;
        }

        QList<int> orders;
        orders.reserve(catIndices.size());
        bool needReindex = false;
        for (qsizetype k = 0; k < catIndices.size(); ++k) {
            const int ord = m_db.items[catIndices[k]].order;
            if (!orders.isEmpty() && ord <= orders.last()) {
                needReindex = true;
            }
            orders.append(ord);
        }
        if (needReindex) {
            for (int i = 0; i < m_db.items.size(); ++i) {
                m_db.items[i].order = i;
            }
            for (qsizetype k = 0; k < catIndices.size(); ++k) {
                orders[k] = m_db.items[catIndices[k]].order;
            }
        }

        catIndices.move(pos, targetPos);
        for (qsizetype k = 0; k < catIndices.size(); ++k) {
            m_db.items[catIndices[k]].order = orders[k];
        }
        sortDatabase(m_db);
        saveLocked();
    }
    emit changed();
    return true;
}

void LauncherStore::recordLaunch(const QString& itemId) {
    bool modified = false;
    {
        QMutexLocker locker(&m_mutex);
        for (auto& it : m_db.items) {
            if (it.id == itemId) {
                it.runCount += 1;
                it.lastRunUnix = QDateTime::currentSecsSinceEpoch();
                saveLocked();
                modified = true;
                break;
            }
        }
    }
    if (modified) {
        emit changed();
    }
}

void LauncherStore::saveWindowSize(double width, double height) {
    if (width <= 0.0 || height <= 0.0) {
        return;
    }
    bool modified = false;
    {
        QMutexLocker locker(&m_mutex);
        if (qFuzzyCompare(m_db.windowWidth, width) &&
            qFuzzyCompare(m_db.windowHeight, height)) {
            return;
        }
        m_db.windowWidth = width;
        m_db.windowHeight = height;
        saveLocked();
        modified = true;
    }
    if (modified) {
        emit changed();
    }
}

bool LauncherStore::exportJson(const QString& destPath, QString* error) const {
    if (destPath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("导出路径不能为空");
        }
        return false;
    }
    LauncherDatabase copy;
    {
        QMutexLocker locker(&m_mutex);
        copy = m_db;
        sortDatabase(copy);
    }
    return writeJsonAtomically(destPath, copy.toJson(), error);
}

bool LauncherStore::importBackupJson(const QString& srcPath, QString* message) {
    QFile file(srcPath);
    if (!file.exists()) {
        if (message) {
            *message = QStringLiteral("备份文件不存在: %1").arg(srcPath);
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (message) {
            *message = QStringLiteral("无法读取备份文件: %1").arg(file.errorString());
        }
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (message) {
            *message = QStringLiteral("备份文件不是有效的 JSON 对象: %1").arg(err.errorString());
        }
        return false;
    }
    const QJsonObject obj = doc.object();
    if (!obj.value(QStringLiteral("categories")).isArray() &&
        !obj.value(QStringLiteral("items")).isArray()) {
        if (message) {
            *message = QStringLiteral("备份文件格式无效：缺少 categories 或 items 数组");
        }
        return false;
    }
    LauncherDatabase imported = LauncherDatabase::fromJson(obj);
    qsizetype catCount = 0;
    qsizetype itemCount = 0;
    {
        QMutexLocker locker(&m_mutex);
        const LauncherDatabase previous = m_db;
        m_db = std::move(imported);
        normalizeDatabaseLocked();
        if (!saveLocked()) {
            m_db = previous;
            if (message) {
                *message = QStringLiteral("保存还原后的数据库失败");
            }
            return false;
        }
        catCount = m_db.categories.size();
        itemCount = m_db.items.size();
    }
    emit changed();
    if (message) {
        *message = QStringLiteral("已还原 %1 个分组和 %2 个启动项").arg(catCount).arg(itemCount);
    }
    return true;
}

} // namespace ql
