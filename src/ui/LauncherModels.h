#pragma once

#include "core/Models.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <optional>

namespace ql {

class CategoryListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CountRole,
        IsAllRole,
        OrderRole,
    };
    Q_ENUM(Roles)

    explicit CategoryListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh(const LauncherDatabase& db);

    Q_INVOKABLE int indexOfId(const QString& id) const;
    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE bool isAllAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    struct Row {
        QString id;
        QString name;
        int count = 0;
        bool isAll = false;
        int order = 0;
    };
    QList<Row> m_rows;
};

class LaunchItemModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        CategoryIdRole,
        CategoryNameRole,
        NameRole,
        TargetRole,
        ArgumentsRole,
        WorkingDirRole,
        IconUrlRole,
        KeywordsRole,
        RemarkRole,
        RunAsAdminRole,
        RunCountRole,
        ShortcutHintRole,
    };
    Q_ENUM(Roles)

    explicit LaunchItemModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMatches(const QList<SearchMatch>& matches);
    std::optional<LaunchItem> itemAt(int row) const;

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    QList<SearchMatch> m_matches;
};

} // namespace ql
