#pragma once

#include "Models.h"

#include <QList>
#include <QString>

namespace ql {

class PinyinMatcher {
public:
    static QString initials(const QString& text);
    static QList<SearchMatch> search(
        const LauncherDatabase& db,
        const QString& query,
        const QString& activeCategoryId,
        bool searchAllWhenNonEmpty = true);
};

} // namespace ql
