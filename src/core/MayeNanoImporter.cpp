#include "MayeNanoImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <set>
#include <utility>

namespace ql {
namespace {

struct MayeCate {
    QString id;
    QString name;
    int order = 0;
};

struct MayeItem {
    QString id;
    QString idCate;
    QString name;
    QString uri;
    QString dir;
    QString param;
    QString icon;
    QString kw;
    QString t;
    QString remark;
    bool runAsAdmin = false;
    int order = 0;
    int runCount = 0;
    qint64 lastRun = 0;
    QJsonObject rawObj;
};

QString jsonIdToString(const QJsonValue& val) {
    if (val.isString()) {
        return val.toString().trimmed();
    }
    if (val.isDouble()) {
        return QString::number(val.toInteger());
    }
    return QString();
}

QString expandMayeVars(QString value, const QString& mp, const QString& mr) {
    if (value.isEmpty()) {
        return value;
    }
    value.replace(QStringLiteral("%mp%"), mp, Qt::CaseInsensitive);
    if (!mr.isEmpty()) {
        value.replace(QStringLiteral("%mr%"), mr, Qt::CaseInsensitive);
    }
    return value;
}

bool isVirtualContainer(const QJsonObject& obj, const QString& uri) {
    if (obj.value(QStringLiteral("isVirtual")).toBool(false) ||
        obj.value(QStringLiteral("virtual")).toBool(false)) {
        return true;
    }
    if (obj.value(QStringLiteral("child")).isArray() ||
        obj.value(QStringLiteral("children")).isArray() ||
        obj.value(QStringLiteral("items")).isArray()) {
        return true;
    }
    const QString typeStr = obj.value(QStringLiteral("type")).toString().trimmed().toLower();
    if (typeStr == QLatin1String("virtual") || typeStr == QLatin1String("folder") ||
        typeStr == QLatin1String("group") || typeStr == QLatin1String("container") ||
        typeStr == QLatin1String("separator") || typeStr == QLatin1String("divider") ||
        typeStr == QLatin1String("cate")) {
        return true;
    }
    const QString u = uri.trimmed().toLower();
    return u.startsWith(QLatin1String("virtual:")) ||
           u.startsWith(QLatin1String("maye:")) ||
           u == QLatin1String("$virtual") ||
           u.startsWith(QLatin1String("$virtual/")) ||
           u.startsWith(QLatin1String("$virtual:")) ||
           u == QLatin1String("$folder") ||
           u == QLatin1String("$group") ||
           u == QLatin1String("$separator") ||
           u == QLatin1String("$divider");
}

bool extractEntryObject(const QJsonValue& entry, QJsonObject& outObj, QString& outKey) {
    if (entry.isArray()) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() < 2 || !pair[1].isObject()) {
            return false;
        }
        outKey = jsonIdToString(pair[0]);
        outObj = pair[1].toObject();
        return true;
    }
    if (entry.isObject()) {
        outObj = entry.toObject();
        outKey = jsonIdToString(outObj.value(QStringLiteral("id")));
        return true;
    }
    return false;
}

QList<MayeCate> parseCategories(const QJsonArray& arr, QStringList& warnings) {
    QList<MayeCate> result;
    result.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj;
        QString keyId;
        if (!extractEntryObject(arr.at(i), obj, keyId)) {
            warnings.append(QStringLiteral("跳过无效的分组记录（索引 %1）").arg(i));
            continue;
        }
        MayeCate mc;
        mc.id = jsonIdToString(obj.value(QStringLiteral("id")));
        if (mc.id.isEmpty()) {
            mc.id = keyId;
        }
        mc.name = obj.value(QStringLiteral("name")).toString().trimmed();
        if (mc.name.isEmpty()) {
            mc.name = QStringLiteral("未命名");
        }
        mc.order = obj.value(QStringLiteral("order")).toInt(i);
        result.append(std::move(mc));
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const MayeCate& a, const MayeCate& b) {
                         return a.order < b.order;
                     });
    return result;
}

QList<MayeItem> parseItems(const QJsonArray& arr, QStringList& warnings) {
    QList<MayeItem> result;
    result.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj;
        QString keyId;
        if (!extractEntryObject(arr.at(i), obj, keyId)) {
            warnings.append(QStringLiteral("跳过无效的启动项记录（索引 %1）").arg(i));
            continue;
        }
        MayeItem mi;
        mi.id = jsonIdToString(obj.value(QStringLiteral("id")));
        if (mi.id.isEmpty()) {
            mi.id = keyId;
        }
        mi.idCate = jsonIdToString(obj.value(QStringLiteral("idCate")));
        mi.name = obj.value(QStringLiteral("name")).toString().trimmed();
        mi.uri = obj.value(QStringLiteral("uri")).toString().trimmed();
        mi.dir = obj.value(QStringLiteral("dir")).toString().trimmed();
        mi.param = obj.value(QStringLiteral("param")).toString().trimmed();
        mi.icon = obj.value(QStringLiteral("icon")).toString().trimmed();
        mi.kw = obj.value(QStringLiteral("kw")).toString().trimmed();
        mi.t = obj.value(QStringLiteral("t")).toString().trimmed();
        mi.remark = obj.value(QStringLiteral("remark")).toString().trimmed();
        mi.runAsAdmin = obj.value(QStringLiteral("admin")).toBool(
            obj.value(QStringLiteral("runAsAdmin")).toBool(false));
        mi.order = obj.value(QStringLiteral("order")).toInt(i);
        mi.runCount = obj.value(QStringLiteral("runCount")).toInt(0);
        mi.lastRun = obj.value(QStringLiteral("time")).toObject()
                         .value(QStringLiteral("lastRun")).toInteger(0);
        mi.rawObj = obj;
        result.append(std::move(mi));
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const MayeItem& a, const MayeItem& b) {
                         return a.order < b.order;
                     });
    return result;
}

bool loadMdbRoot(const QString& path, QJsonObject& outRoot, QStringList& warnings) {
    if (path.trimmed().isEmpty()) {
        warnings.append(QStringLiteral("MayeNano 数据文件路径为空"));
        return false;
    }
    QFile file(path);
    if (!file.exists()) {
        warnings.append(QStringLiteral("MayeNano 数据文件不存在: %1").arg(path));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        warnings.append(QStringLiteral("无法打开 MayeNano 数据文件: %1").arg(file.errorString()));
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        warnings.append(QStringLiteral("MayeNano 数据文件不是有效的 JSON 对象: %1").arg(err.errorString()));
        return false;
    }
    outRoot = doc.object();
    if (!outRoot.value(QStringLiteral("cate")).isArray() &&
        !outRoot.value(QStringLiteral("item")).isArray()) {
        warnings.append(QStringLiteral("MDB.json 缺少有效的 cate 或 item 数组"));
        return false;
    }
    return true;
}

LaunchItem toLaunchItem(const MayeItem& mi, const QString& storeCatId,
                        const QString& mp, const QString& mr) {
    LaunchItem item;
    item.categoryId = storeCatId;
    item.target = expandMayeVars(mi.uri, mp, mr);
    item.arguments = expandMayeVars(mi.param, mp, mr);
    item.workingDir = expandMayeVars(mi.dir, mp, mr);
    item.iconPath = expandMayeVars(mi.icon, mp, mr);
    item.name = mi.name.isEmpty()
        ? QFileInfo(item.target).completeBaseName()
        : mi.name;
    if (item.name.isEmpty()) {
        item.name = item.target;
    }
    QStringList kwParts;
    if (!mi.kw.isEmpty()) {
        kwParts.append(mi.kw);
    }
    if (!mi.t.isEmpty() && mi.t != mi.kw) {
        kwParts.append(mi.t);
    }
    item.keywords = kwParts.join(u' ');
    item.remark = mi.remark;
    item.runAsAdmin = mi.runAsAdmin;
    item.runCount = std::max(0, mi.runCount);
    item.lastRunUnix = std::max<qint64>(0, mi.lastRun);
    return item;
}

} // namespace

MayeImportResult MayeNanoImporter::importFromFile(const QString& mdbJsonPath, LauncherStore& store) {
    MayeImportResult result;
    QJsonObject root;
    if (!loadMdbRoot(mdbJsonPath, root, result.warnings)) {
        return result;
    }

    QString mp = QDir::toNativeSeparators(QFileInfo(mdbJsonPath).absolutePath());
    while (mp.endsWith(u'\\') || mp.endsWith(u'/')) {
        mp.chop(1);
    }
    const QString mr = (mp.size() >= 2 && mp[1] == u':') ? mp.left(2) : QString();

    const QList<MayeCate> cates = parseCategories(
        root.value(QStringLiteral("cate")).toArray(), result.warnings);
    const QList<MayeItem> mayeItems = parseItems(
        root.value(QStringLiteral("item")).toArray(), result.warnings);

    LauncherDatabase snap = store.snapshot();
    QHash<QString, QString> nameToStoreId;
    for (const auto& cat : snap.categories) {
        nameToStoreId.insert(cat.name.trimmed(), cat.id);
    }

    QHash<QString, QString> mayeIdToStoreId;
    for (const auto& mc : cates) {
        if (nameToStoreId.contains(mc.name)) {
            mayeIdToStoreId.insert(mc.id, nameToStoreId.value(mc.name));
        } else {
            const CategoryEntry created = store.addCategory(mc.name);
            mayeIdToStoreId.insert(mc.id, created.id);
            nameToStoreId.insert(mc.name, created.id);
            result.addedCategories += 1;
        }
    }

    snap = store.snapshot();
    const QString fallbackCatId = snap.categories.isEmpty() ? QString() : snap.categories.first().id;
    using TargetArgKey = std::pair<QString, QString>;
    std::set<TargetArgKey> seenTargetArgs;
    for (const auto& existing : snap.items) {
        seenTargetArgs.insert({
            existing.target.trimmed().toLower(),
            existing.arguments.trimmed().toLower()
        });
    }

    QList<LaunchItem> toAdd;
    toAdd.reserve(mayeItems.size());
    for (const auto& mi : mayeItems) {
        const QString display = mi.name.isEmpty() ? mi.id : mi.name;
        if (isVirtualContainer(mi.rawObj, mi.uri)) {
            result.warnings.append(QStringLiteral("跳过虚拟容器条目: %1").arg(display));
            continue;
        }
        if (mi.uri.isEmpty()) {
            result.warnings.append(QStringLiteral("跳过空目标条目: %1").arg(display));
            continue;
        }
        const QString storeCatId = mayeIdToStoreId.value(mi.idCate, fallbackCatId);
        LaunchItem item = toLaunchItem(mi, storeCatId, mp, mr);
        const TargetArgKey key{
            item.target.trimmed().toLower(),
            item.arguments.trimmed().toLower()
        };
        if (!seenTargetArgs.insert(key).second) {
            result.skippedDuplicates += 1;
            continue;
        }
        toAdd.append(std::move(item));
    }

    const int added = store.addItems(toAdd);
    result.addedItems = added;
    if (added < toAdd.size()) {
        result.skippedDuplicates += static_cast<int>(toAdd.size() - added);
    }
    return result;
}

} // namespace ql
