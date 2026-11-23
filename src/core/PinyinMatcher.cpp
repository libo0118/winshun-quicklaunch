#include "PinyinMatcher.h"

#include <QFileInfo>
#include <QHash>
#include <algorithm>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ql {
namespace {

char lookupOverride(char16_t code) {
    static const QHash<char16_t, char> kOverrides = {
        // 高频软件与系统常用字
        {u'极', 'j'}, {u'讯', 'x'}, {u'微', 'w'}, {u'飞', 'f'}, {u'腾', 't'},
        {u'顺', 's'}, {u'宝', 'b'}, {u'览', 'l'}, {u'画', 'h'}, {u'截', 'j'},
        {u'译', 'y'}, {u'搜', 's'}, {u'快', 'k'}, {u'控', 'k'}, {u'记', 'j'},
        {u'脑', 'n'}, {u'端', 'd'}, {u'盘', 'p'}, {u'网', 'w'}, {u'软', 'r'},
        {u'邮', 'y'},
        // GBK 二级及补充常用字
        {u'哔', 'b'}, {u'咪', 'm'}, {u'昕', 'x'}, {u'昵', 'n'}, {u'炫', 'x'},
        {u'魅', 'm'}, {u'嗨', 'h'}, {u'喵', 'm'}, {u'嘀', 'd'}, {u'嘟', 'd'},
        {u'哒', 'd'}, {u'嗯', 'e'}, {u'啰', 'l'}, {u'睿', 'r'}, {u'鑫', 'x'},
        {u'曦', 'x'}, {u'岚', 'l'}, {u'琪', 'q'}, {u'琦', 'q'}, {u'珺', 'j'},
        {u'璟', 'j'}, {u'瑾', 'j'}, {u'瑜', 'y'}, {u'璇', 'x'}, {u'璐', 'l'},
        {u'晟', 's'}, {u'昊', 'h'}, {u'昱', 'y'}, {u'晔', 'y'}, {u'昀', 'y'},
        {u'晖', 'h'}, {u'炜', 'w'}, {u'煜', 'y'}, {u'煊', 'x'}, {u'烁', 's'},
        {u'烨', 'y'}, {u'淼', 'm'}, {u'焱', 'y'}, {u'垚', 'y'}, {u'梓', 'z'},
        {u'茗', 'm'}, {u'兮', 'x'}, {u'筱', 'x'}, {u'囧', 'j'}, {u'咔', 'k'},
        {u'咚', 'd'}, {u'咣', 'g'}, {u'哐', 'k'}, {u'咻', 'x'}, {u'嗖', 's'},
        {u'嚓', 'c'}, {u'撸', 'l'},
        // 常见繁体软件名字符
        {u'啟', 'q'}, {u'網', 'w'}, {u'軟', 'r'}, {u'郵', 'y'}, {u'電', 'd'},
        {u'腦', 'n'}, {u'圖', 't'}, {u'畫', 'h'}, {u'譯', 'y'}, {u'覽', 'l'},
        {u'訊', 'x'}, {u'飛', 'f'}, {u'極', 'j'}, {u'寶', 'b'}, {u'順', 's'},
        {u'記', 'j'}, {u'盤', 'p'}, {u'終', 'z'}, {u'機', 'j'}, {u'檔', 'd'},
        {u'資', 'z'}, {u'夾', 'j'}, {u'設', 's'}, {u'視', 's'}, {u'頻', 'p'},
        {u'樂', 'l'}, {u'開', 'k'}, {u'關', 'g'}, {u'編', 'b'}, {u'輯', 'j'},
        {u'壓', 'y'}, {u'縮', 's'}, {u'統', 't'}, {u'計', 'j'}, {u'語', 'y'},
        {u'歷', 'l'}, {u'時', 's'}, {u'鐘', 'z'}, {u'顯', 'x'}, {u'驅', 'q'},
        {u'動', 'd'}, {u'檢', 'j'}, {u'測', 'c'}, {u'試', 's'}, {u'調', 't'},
        {u'發', 'f'}, {u'務', 'w'}, {u'備', 'b'}, {u'還', 'h'}, {u'鎖', 's'},
        {u'註', 'z'}, {u'銷', 'x'}
    };
    return kOverrides.value(code, '\0');
}

char gb2312Initial(char16_t uc) {
    const wchar_t wch = static_cast<wchar_t>(uc);
    char buf[4] = {};
    BOOL usedDefault = FALSE;
    const int bytes = ::WideCharToMultiByte(
        936, 0, &wch, 1, buf, static_cast<int>(sizeof(buf)), nullptr, &usedDefault);
    if (bytes != 2 || usedDefault) {
        return '\0';
    }
    const unsigned int code =
        (static_cast<unsigned int>(static_cast<unsigned char>(buf[0])) << 8) |
        static_cast<unsigned int>(static_cast<unsigned char>(buf[1]));

    static constexpr struct Range {
        unsigned short lo;
        unsigned short hi;
        char py;
    } kRanges[] = {
        {0xB0A1, 0xB0C4, 'a'}, {0xB0C5, 0xB2C0, 'b'}, {0xB2C1, 0xB4ED, 'c'},
        {0xB4EE, 0xB6E9, 'd'}, {0xB6EA, 0xB7A1, 'e'}, {0xB7A2, 0xB8C0, 'f'},
        {0xB8C1, 0xB9FD, 'g'}, {0xB9FE, 0xBBF6, 'h'}, {0xBBF7, 0xBFA5, 'j'},
        {0xBFA6, 0xC0AB, 'k'}, {0xC0AC, 0xC2E7, 'l'}, {0xC2E8, 0xC4C2, 'm'},
        {0xC4C3, 0xC5B5, 'n'}, {0xC5B6, 0xC5BD, 'o'}, {0xC5BE, 0xC6D9, 'p'},
        {0xC6DA, 0xC8BA, 'q'}, {0xC8BB, 0xC8F5, 'r'}, {0xC8F6, 0xCBF9, 's'},
        {0xCBFA, 0xCDD9, 't'}, {0xCDDA, 0xCEF3, 'w'}, {0xCEF4, 0xD1B8, 'x'},
        {0xD1B9, 0xD4D0, 'y'}, {0xD4D1, 0xD7F9, 'z'}
    };
    for (const auto& r : kRanges) {
        if (code >= r.lo && code <= r.hi) {
            return r.py;
        }
    }
    return '\0';
}

QString extractFileNameLower(const QString& target) {
    QString cleaned = target.trimmed();
    while (cleaned.size() > 1 && (cleaned.endsWith(u'\\') || cleaned.endsWith(u'/'))) {
        cleaned.chop(1);
    }
    if (cleaned.size() == 2 && cleaned[1] == u':') {
        return QString();
    }
    return QFileInfo(cleaned).fileName().toLower();
}

PinyinMatcher::IndexedEntry buildIndexedEntry(const LaunchItem& item) {
    PinyinMatcher::IndexedEntry idx;
    idx.item = item;
    idx.nameLower = item.name.toLower();
    idx.nameInitials = PinyinMatcher::initials(item.name);
    idx.kwLower = item.keywords.toLower();
    idx.kwInitials = PinyinMatcher::initials(item.keywords);
    idx.remarkLower = item.remark.toLower();
    idx.remarkInitials = PinyinMatcher::initials(item.remark);
    idx.targetLower = item.target.toLower();
    idx.fileNameLower = extractFileNameLower(item.target);
    return idx;
}

int scoreToken(const PinyinMatcher::IndexedEntry& idx, const QString& tok) {
    if (idx.nameLower == tok) return 100;
    if (idx.nameLower.startsWith(tok)) return 92;
    if (idx.nameLower.contains(tok)) return 80;
    if (idx.nameInitials.startsWith(tok)) return 72;
    if (idx.nameInitials.contains(tok)) return 64;
    if (idx.kwLower.contains(tok) || idx.kwInitials.contains(tok) ||
        idx.fileNameLower.contains(tok)) {
        return 54;
    }
    if (idx.remarkLower.contains(tok) || idx.remarkInitials.contains(tok) ||
        idx.targetLower.contains(tok)) {
        return 42;
    }
    return 0;
}

} // namespace

QString PinyinMatcher::initials(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        const char16_t uc = ch.unicode();
        if (uc >= u'A' && uc <= u'Z') {
            result.append(QChar(uc + (u'a' - u'A')));
        } else if ((uc >= u'a' && uc <= u'z') || (uc >= u'0' && uc <= u'9')) {
            result.append(ch);
        } else if (const char ov = lookupOverride(uc); ov != '\0') {
            result.append(QLatin1Char(ov));
        } else if (const char py = gb2312Initial(uc); py != '\0') {
            result.append(QLatin1Char(py));
        }
    }
    return result;
}

PinyinMatcher::SearchIndex PinyinMatcher::buildIndex(const LauncherDatabase& db) {
    SearchIndex index;
    index.categoryNames.reserve(db.categories.size());
    for (const auto& cat : db.categories) {
        index.categoryNames.insert(cat.id, cat.name);
    }
    index.entries.reserve(db.items.size());
    for (const auto& item : db.items) {
        index.entries.append(buildIndexedEntry(item));
    }
    return index;
}

QList<SearchMatch> PinyinMatcher::search(
    const SearchIndex& index,
    const QString& query,
    const QString& activeCategoryId,
    bool searchAllWhenNonEmpty) {
    const QString simplified = query.simplified();
    const bool allCategories =
        activeCategoryId.isEmpty() || activeCategoryId == QLatin1String("__all__");
    QList<SearchMatch> matches;

    if (simplified.isEmpty()) {
        for (const auto& entry : index.entries) {
            if (!allCategories && entry.item.categoryId != activeCategoryId) {
                continue;
            }
            matches.append(SearchMatch{
                entry.item, index.categoryNames.value(entry.item.categoryId), 0});
        }
        std::stable_sort(matches.begin(), matches.end(),
                         [](const SearchMatch& a, const SearchMatch& b) {
                             if (a.item.order != b.item.order) {
                                 return a.item.order < b.item.order;
                             }
                             return a.item.runCount > b.item.runCount;
                         });
        return matches;
    }

    const QString fullLower = simplified.toLower();
    const QStringList tokens = fullLower.split(u' ', Qt::SkipEmptyParts);
    const bool filterByCat = !searchAllWhenNonEmpty && !allCategories;

    for (const auto& entry : index.entries) {
        if (filterByCat && entry.item.categoryId != activeCategoryId) {
            continue;
        }
        int totalScore = 0;
        bool allMatched = true;
        for (const auto& tok : tokens) {
            const int s = scoreToken(entry, tok);
            if (s <= 0) {
                allMatched = false;
                break;
            }
            totalScore += s;
        }
        if (!allMatched) {
            continue;
        }
        int baseScore = totalScore / static_cast<int>(tokens.size());
        if (entry.nameLower == fullLower) {
            baseScore = std::max(baseScore, 100);
        } else if (entry.nameLower.startsWith(fullLower)) {
            baseScore = std::max(baseScore, 92);
        } else if (entry.nameLower.contains(fullLower)) {
            baseScore = std::max(baseScore, 80);
        }
        const int bonus = std::clamp(entry.item.runCount, 0, 8);
        const int finalScore = std::min(100, baseScore + bonus);
        matches.append(SearchMatch{
            entry.item, index.categoryNames.value(entry.item.categoryId), finalScore});
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [](const SearchMatch& a, const SearchMatch& b) {
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         if (a.item.runCount != b.item.runCount) {
                             return a.item.runCount > b.item.runCount;
                         }
                         return a.item.order < b.item.order;
                     });
    return matches;
}

QList<SearchMatch> PinyinMatcher::search(
    const LauncherDatabase& db,
    const QString& query,
    const QString& activeCategoryId,
    bool searchAllWhenNonEmpty) {
    return search(buildIndex(db), query, activeCategoryId, searchAllWhenNonEmpty);
}

} // namespace ql
