#pragma once

#include "Models.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <optional>

namespace ql {

class LauncherStore : public QObject {
    Q_OBJECT

public:
    explicit LauncherStore(QString dataDir, QObject* parent = nullptr);

    QString dataDir() const;
    QString dbPath() const;

    LauncherDatabase loadOrInitialize();
    LauncherDatabase snapshot() const;
    std::optional<LaunchItem> findById(const QString& itemId) const;

    CategoryEntry addCategory(const QString& name);
    bool renameCategory(const QString& id, const QString& newName);
    bool deleteCategory(const QString& id, bool moveItemsToDefault = true);
    bool moveCategoryOrder(const QString& id, int delta);
    void setActiveCategory(const QString& categoryId);

    LaunchItem addItem(LaunchItem item);
    int addItems(const QList<LaunchItem>& items);
    bool updateItem(const LaunchItem& updated);
    bool deleteItem(const QString& itemId);
    bool moveItemToCategory(const QString& itemId, const QString& targetCategoryId);
    bool moveItemOrder(const QString& itemId, int delta);
    void recordLaunch(const QString& itemId);
    void saveWindowSize(double width, double height);

    bool exportJson(const QString& destPath, QString* error = nullptr) const;
    bool importBackupJson(const QString& srcPath, QString* message = nullptr);

signals:
    void changed();

private:
    static LauncherDatabase makeDefaultDatabase();
    static void sortDatabase(LauncherDatabase& db);
    static QString generateId();
    void normalizeDatabaseLocked();
    bool saveLocked() const;
    bool backupCorruptFile(const QString& sourcePath) const;

    QString m_dataDir;
    mutable QMutex m_mutex;
    LauncherDatabase m_db;
};

} // namespace ql
