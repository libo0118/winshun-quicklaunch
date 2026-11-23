#include "ui/LauncherModels.h"

#include <QHash>
#include <QUrl>

#include <algorithm>

namespace ql {

CategoryListModel::CategoryListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int CategoryListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant CategoryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case IdRole: return row.id;
    case NameRole:
    case Qt::DisplayRole: return row.name;
    case CountRole: return row.count;
    case IsAllRole: return row.isAll;
    case OrderRole: return row.order;
    default: return {};
    }
}

QHash<int, QByteArray> CategoryListModel::roleNames() const
{
    return {
        {IdRole, "categoryId"},
        {NameRole, "name"},
        {CountRole, "count"},
        {IsAllRole, "isAll"},
        {OrderRole, "order"},
    };
}

void CategoryListModel::refresh(const LauncherDatabase& db)
{
    QHash<QString, int> counts;
    for (const auto& item : db.items) {
        ++counts[item.categoryId];
    }

    QList<CategoryEntry> cats = db.categories;
    std::stable_sort(cats.begin(), cats.end(), [](const CategoryEntry& a, const CategoryEntry& b) {
        return a.order < b.order;
    });

    QList<Row> nextRows;
    nextRows.reserve(cats.size() + 1);
    nextRows.append(Row{
        QStringLiteral("__all__"),
        QStringLiteral("全部"),
        static_cast<int>(db.items.size()),
        true,
        -1
    });
    for (const auto& c : cats) {
        nextRows.append(Row{c.id, c.name, counts.value(c.id, 0), false, c.order});
    }

    beginResetModel();
    m_rows = std::move(nextRows);
    endResetModel();
}

int CategoryListModel::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return i;
        }
    }
    return 0;
}
QString CategoryListModel::idAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).id : QString();
}

QString CategoryListModel::nameAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).name : QString();
}

bool CategoryListModel::isAllAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).isAll : false;
}

QVariantMap CategoryListModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    const Row& r = m_rows.at(row);
    return {
        {QStringLiteral("categoryId"), r.id},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("count"), r.count},
        {QStringLiteral("isAll"), r.isAll},
        {QStringLiteral("order"), r.order},
    };
}

LaunchItemModel::LaunchItemModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int LaunchItemModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_matches.size());
}

QVariant LaunchItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_matches.size()) {
        return {};
    }
    const SearchMatch& m = m_matches.at(index.row());
    const LaunchItem& it = m.item;
    switch (role) {
    case IdRole: return it.id;
    case CategoryIdRole: return it.categoryId;
    case CategoryNameRole: return m.categoryName;
    case NameRole:
    case Qt::DisplayRole: return it.name;
    case TargetRole: return it.target;
    case ArgumentsRole: return it.arguments;
    case WorkingDirRole: return it.workingDir;
    case IconUrlRole: {
        const QString iconKey = it.iconPath.trimmed().isEmpty()
            ? it.target.trimmed()
            : it.iconPath.trimmed();
        if (iconKey.isEmpty()) {
            return QString();
        }
        return QStringLiteral("image://appicon/") +
               QString::fromLatin1(QUrl::toPercentEncoding(iconKey));
    }
    case KeywordsRole: return it.keywords;
    case RemarkRole: return it.remark;
    case RunAsAdminRole: return it.runAsAdmin;
    case RunCountRole: return it.runCount;
    case ShortcutHintRole:
        return (index.row() >= 0 && index.row() < 9)
            ? QStringLiteral("Ctrl+%1").arg(index.row() + 1)
            : QString();
    default: return {};
    }
}
QHash<int, QByteArray> LaunchItemModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {CategoryIdRole, "categoryId"},
        {CategoryNameRole, "categoryName"},
        {NameRole, "name"},
        {TargetRole, "target"},
        {ArgumentsRole, "arguments"},
        {WorkingDirRole, "workingDir"},
        {IconUrlRole, "iconUrl"},
        {KeywordsRole, "keywords"},
        {RemarkRole, "remark"},
        {RunAsAdminRole, "runAsAdmin"},
        {RunCountRole, "runCount"},
        {ShortcutHintRole, "shortcutHint"},
    };
}

void LaunchItemModel::setMatches(const QList<SearchMatch>& matches)
{
    beginResetModel();
    m_matches = matches;
    endResetModel();
}

std::optional<LaunchItem> LaunchItemModel::itemAt(int row) const
{
    if (row < 0 || row >= m_matches.size()) {
        return std::nullopt;
    }
    return m_matches.at(row).item;
}

QString LaunchItemModel::idAt(int row) const
{
    return (row >= 0 && row < m_matches.size()) ? m_matches.at(row).item.id : QString();
}

QVariantMap LaunchItemModel::get(int row) const
{
    if (row < 0 || row >= m_matches.size()) {
        return {};
    }
    const SearchMatch& m = m_matches.at(row);
    const LaunchItem& it = m.item;
    return {
        {QStringLiteral("itemId"), it.id},
        {QStringLiteral("categoryId"), it.categoryId},
        {QStringLiteral("categoryName"), m.categoryName},
        {QStringLiteral("name"), it.name},
        {QStringLiteral("target"), it.target},
        {QStringLiteral("arguments"), it.arguments},
        {QStringLiteral("workingDir"), it.workingDir},
        {QStringLiteral("iconPath"), it.iconPath},
        {QStringLiteral("keywords"), it.keywords},
        {QStringLiteral("remark"), it.remark},
        {QStringLiteral("runAsAdmin"), it.runAsAdmin},
        {QStringLiteral("runCount"), it.runCount},
        {QStringLiteral("order"), it.order},
    };
}

} // namespace ql
