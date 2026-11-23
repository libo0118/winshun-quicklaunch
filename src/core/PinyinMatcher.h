#pragma once

#include "Models.h"

#include <QHash>
#include <QList>
#include <QString>

namespace ql {

class PinyinMatcher {
public:
    struct IndexedEntry {
        LaunchItem item;
        QString nameLower;
        QString nameInitials;
        QString kwLower;
        QString kwInitials;
        QString remarkLower;
        QString remarkInitials;
        QString targetLower;
        QString fileNameLower;
    };
    struct SearchIndex {
        QHash<QString, QString> categoryNames;
        QList<IndexedEntry> entries;
    };

    static QString initials(const QString& text);
    static SearchIndex buildIndex(const LauncherDatabase& db);
    static QList<SearchMatch> search(
        const SearchIndex& index,
        const QString& query,
        const QString& activeCategoryId,
        bool searchAllWhenNonEmpty = true);
    static QList<SearchMatch> search(
        const LauncherDatabase& db,
        const QString& query,
        const QString& activeCategoryId,
        bool searchAllWhenNonEmpty = true);
};

} // namespace ql
