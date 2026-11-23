#include "core/LauncherStore.h"
#include "core/MayeNanoImporter.h"
#include "core/Models.h"
#include "core/PinyinMatcher.h"
#include "platform/ShellBridge.h"
#include "ui/ThemeObject.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void failAt(const char* expr, const char* file, int line, const QString& detail = {})
{
    ++g_failures;
    std::cerr << "[FAIL] " << file << ":" << line << " (" << expr << ")";
    if (!detail.isEmpty()) {
        std::cerr << " - " << detail.toStdString();
    }
    std::cerr << "\n";
}

#define QL_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failAt(#cond, __FILE__, __LINE__);                                                     \
            return;                                                                                \
        }                                                                                          \
    } while (false)

#define QL_CHECK_MSG(cond, msg)                                                                    \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failAt(#cond, __FILE__, __LINE__, (msg));                                              \
            return;                                                                                \
        }                                                                                          \
    } while (false)

void testLauncherStore()
{
    QTemporaryDir tempDir;
    QL_CHECK(tempDir.isValid());

    {
        ql::LauncherStore store(tempDir.path());
        const ql::LauncherDatabase initDb = store.loadOrInitialize();
        QL_CHECK(initDb.categories.size() == 1);
        QL_CHECK(initDb.categories.first().id == QStringLiteral("default"));
        QL_CHECK(initDb.items.size() == 4);
        QL_CHECK(QFile::exists(store.dbPath()));

        const ql::CategoryEntry workCat = store.addCategory(QStringLiteral("办公"));
        const ql::CategoryEntry devCat = store.addCategory(QStringLiteral("开发"));
        QL_CHECK(!workCat.id.isEmpty() && !devCat.id.isEmpty());
        QL_CHECK(store.renameCategory(workCat.id, QStringLiteral("日常办公")));
        QL_CHECK(store.moveCategoryOrder(devCat.id, -1));
        const ql::LauncherDatabase afterOrder = store.snapshot();
        QL_CHECK(afterOrder.categories.size() == 3);
        QL_CHECK(afterOrder.categories.at(1).id == devCat.id);

        ql::LaunchItem itemA;
        itemA.categoryId = devCat.id;
        itemA.name = QStringLiteral("示例工具");
        itemA.target = QStringLiteral("C:\\Windows\\notepad.exe");
        itemA.arguments = QStringLiteral("--test");
        itemA.keywords = QStringLiteral("slgj");
        const ql::LaunchItem addedA = store.addItem(itemA);
        QL_CHECK(!addedA.id.isEmpty());

        ql::LaunchItem dupItem = itemA;
        dupItem.id.clear();
        dupItem.target = QStringLiteral("  c:\\windows\\NOTEPAD.EXE ");
        dupItem.arguments = QStringLiteral(" --TEST ");
        ql::LaunchItem uniqueItem;
        uniqueItem.categoryId = devCat.id;
        uniqueItem.name = QStringLiteral("计算器");
        uniqueItem.target = QStringLiteral("calc.exe");
        const int batchAdded = store.addItems({dupItem, uniqueItem});
        QL_CHECK(batchAdded == 1);

        QL_CHECK(store.moveItemOrder(uniqueItem.id.isEmpty()
            ? store.snapshot().items.last().id : uniqueItem.id, -1));
        ql::LaunchItem updated = addedA;
        updated.remark = QStringLiteral("已更新备注");
        QL_CHECK(store.updateItem(updated));
        store.recordLaunch(addedA.id);
        const auto foundOpt = store.findById(addedA.id);
        QL_CHECK(foundOpt.has_value());
        QL_CHECK(foundOpt->remark == QStringLiteral("已更新备注"));
        QL_CHECK(foundOpt->runCount == 1);

        QL_CHECK(store.deleteCategory(devCat.id, true));
        const ql::LauncherDatabase afterCatDel = store.snapshot();
        QL_CHECK(afterCatDel.categories.size() == 2);
        QL_CHECK(store.findById(addedA.id)->categoryId == QStringLiteral("default"));
        QL_CHECK(store.deleteItem(addedA.id));
    }

    {
        ql::LauncherStore reloaded(tempDir.path());
        const ql::LauncherDatabase db2 = reloaded.loadOrInitialize();
        QL_CHECK(db2.categories.size() == 2);
        QL_CHECK(db2.items.size() == 5);
        QL_CHECK(reloaded.deleteCategory(QStringLiteral("default"), true));
        QL_CHECK(reloaded.snapshot().categories.size() == 1);
        QL_CHECK(!reloaded.deleteCategory(reloaded.snapshot().categories.first().id, true));

        QFile corruptFile(reloaded.dbPath());
        QL_CHECK(corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corruptFile.write("{broken-json");
        corruptFile.close();

        const ql::LauncherDatabase recovered = reloaded.loadOrInitialize();
        QL_CHECK(recovered.categories.size() == 1);
        QL_CHECK(recovered.items.size() == 4);
        const QStringList bakFiles = QDir(tempDir.path()).entryList(
            {QStringLiteral("launcher-db.corrupt.*.bak")}, QDir::Files);
        QL_CHECK(bakFiles.size() >= 1);
    }
}

void testPinyinMatcher()
{
    ql::LauncherDatabase db;
    db.activeCategoryId = QStringLiteral("c1");
    db.categories.append(ql::CategoryEntry{QStringLiteral("c1"), QStringLiteral("常用"), 0});
    db.categories.append(ql::CategoryEntry{QStringLiteral("c2"), QStringLiteral("系统"), 1});

    ql::LaunchItem notepad;
    notepad.id = QStringLiteral("i1");
    notepad.categoryId = QStringLiteral("c1");
    notepad.name = QStringLiteral("记事本");
    notepad.target = QStringLiteral("C:\\Windows\\notepad.exe");
    notepad.keywords = QStringLiteral("editor txt");
    notepad.order = 0;

    ql::LaunchItem pc;
    pc.id = QStringLiteral("i2");
    pc.categoryId = QStringLiteral("c2");
    pc.name = QStringLiteral("此电脑");
    pc.target = QStringLiteral("$computer");
    pc.order = 1;

    ql::LaunchItem ctrl;
    ctrl.id = QStringLiteral("i3");
    ctrl.categoryId = QStringLiteral("c2");
    ctrl.name = QStringLiteral("控制面板");
    ctrl.target = QStringLiteral("$controls");
    ctrl.order = 2;

    ql::LaunchItem devTool;
    devTool.id = QStringLiteral("i4");
    devTool.categoryId = QStringLiteral("c1");
    devTool.name = QStringLiteral("VS Code Build");
    devTool.target = QStringLiteral("D:\\Tools\\Code.exe");
    devTool.remark = QStringLiteral("开发编辑器");
    devTool.order = 3;

    db.items = {notepad, pc, ctrl, devTool};

    QL_CHECK(ql::PinyinMatcher::initials(QStringLiteral("记事本")) == QStringLiteral("jsb"));
    QL_CHECK(ql::PinyinMatcher::initials(QStringLiteral("此电脑")) == QStringLiteral("cdn"));
    QL_CHECK(ql::PinyinMatcher::initials(QStringLiteral("控制面板")).startsWith(QStringLiteral("kzm")));

    const auto c1Only = ql::PinyinMatcher::search(db, QString(), QStringLiteral("c1"));
    QL_CHECK(c1Only.size() == 2);
    const auto allEmpty = ql::PinyinMatcher::search(db, QString(), QStringLiteral("__all__"));
    QL_CHECK(allEmpty.size() == 4);

    const auto byZh = ql::PinyinMatcher::search(db, QStringLiteral("记事本"), QStringLiteral("c2"));
    QL_CHECK(byZh.size() == 1 && byZh.first().item.id == QStringLiteral("i1"));

    const auto byPrefix = ql::PinyinMatcher::search(db, QStringLiteral("VS"), QStringLiteral("c1"));
    QL_CHECK(byPrefix.size() == 1 && byPrefix.first().item.id == QStringLiteral("i4"));

    const auto byMulti = ql::PinyinMatcher::search(db, QStringLiteral("code kfbjq"), QStringLiteral("c1"));
    QL_CHECK(byMulti.size() == 1 && byMulti.first().item.id == QStringLiteral("i4"));

    const auto byJsb = ql::PinyinMatcher::search(db, QStringLiteral("jsb"), QStringLiteral("c1"));
    QL_CHECK(byJsb.size() == 1 && byJsb.first().item.id == QStringLiteral("i1"));

    const auto byCdn = ql::PinyinMatcher::search(db, QStringLiteral("cdn"), QStringLiteral("c1"));
    QL_CHECK(byCdn.size() == 1 && byCdn.first().item.id == QStringLiteral("i2"));

    const auto byKzm = ql::PinyinMatcher::search(db, QStringLiteral("kzm"), QStringLiteral("c1"));
    QL_CHECK(byKzm.size() == 1 && byKzm.first().item.id == QStringLiteral("i3"));

    const auto specialEmpty = ql::PinyinMatcher::search(
        db, QStringLiteral("!@#$%^&*()___not_found"), QStringLiteral("__all__"));
    QL_CHECK(specialEmpty.isEmpty());
}

void testShellBridge()
{
    const QString expanded = ql::ShellBridge::expandPath(QStringLiteral("%WINDIR%\\notepad.exe"));
    QL_CHECK(!expanded.contains(u'%'));
    QL_CHECK(expanded.endsWith(QStringLiteral("\\notepad.exe"), Qt::CaseInsensitive));

    QL_CHECK(ql::ShellBridge::isWebUrl(QStringLiteral("https://example.com/a")));
    QL_CHECK(ql::ShellBridge::isWebUrl(QStringLiteral("HTTP://localhost:8080")));
    QL_CHECK(ql::ShellBridge::isWebUrl(QStringLiteral("mailto:test@example.com")));
    QL_CHECK(!ql::ShellBridge::isWebUrl(QStringLiteral("C:\\Windows\\notepad.exe")));

    QL_CHECK(ql::ShellBridge::isSpecialSystemUri(QStringLiteral("$computer")));
    QL_CHECK(ql::ShellBridge::isSpecialSystemUri(QStringLiteral(" $CONTROLS ")));
    QL_CHECK(!ql::ShellBridge::isSpecialSystemUri(QStringLiteral("cmd.exe")));

    ql::LaunchItem plainAbs;
    plainAbs.id = QStringLiteral("plain");
    plainAbs.target = QStringLiteral("C:\\Windows\\notepad.exe");
    QL_CHECK(!ql::ShellBridge::needsShimLnk(plainAbs, false));
    QL_CHECK(ql::ShellBridge::needsShimLnk(plainAbs, true));

    ql::LaunchItem bareName;
    bareName.id = QStringLiteral("bare");
    bareName.target = QStringLiteral("notepad.exe");
    QL_CHECK(ql::ShellBridge::needsShimLnk(bareName, false));

    ql::LaunchItem webItem;
    webItem.id = QStringLiteral("web");
    webItem.target = QStringLiteral("https://github.com");
    QL_CHECK(!ql::ShellBridge::needsShimLnk(webItem, false));

    ql::LaunchItem specialItem;
    specialItem.id = QStringLiteral("sys");
    specialItem.target = QStringLiteral("$computer");
    QL_CHECK(ql::ShellBridge::needsShimLnk(specialItem, false));

    ql::LaunchItem argsItem = plainAbs;
    argsItem.arguments = QStringLiteral("--flag");
    QL_CHECK(ql::ShellBridge::needsShimLnk(argsItem, false));

    QTemporaryDir tempDir;
    QL_CHECK(tempDir.isValid());
    ql::LaunchItem shimItem;
    shimItem.id = QStringLiteral("shim_1");
    shimItem.target = QStringLiteral("%WINDIR%\\notepad.exe");
    shimItem.arguments = QStringLiteral("--unit-test");
    shimItem.workingDir = QStringLiteral("%TEMP%");
    QString err;
    const QString lnkPath = ql::ShellBridge::ensureLaunchLnk(tempDir.path(), shimItem, true, &err);
    QL_CHECK_MSG(!lnkPath.isEmpty() && QFile::exists(lnkPath), err);
    const auto resolved = ql::ShellBridge::resolveLnk(lnkPath);
    QL_CHECK(resolved.has_value());
    QL_CHECK(resolved->target.endsWith(QStringLiteral("notepad.exe"), Qt::CaseInsensitive));
    QL_CHECK(resolved->arguments == QStringLiteral("--unit-test"));
    QL_CHECK(resolved->runAsAdmin);
}

void testThemeData()
{
    const ql::ThemeData defDark;
    QL_CHECK(defDark.dark && defDark.rounded);
    QL_CHECK(defDark.cardRadius == 10.0 && defDark.controlRadius == 6.0);

    const ql::ThemeData lightSquare = ql::ThemeData::fromAppearance(
        QStringLiteral("en"), QStringLiteral("light"),
        QJsonObject{{QStringLiteral("corners"), QStringLiteral("square")}});
    QL_CHECK(!lightSquare.dark && !lightSquare.rounded);
    QL_CHECK(lightSquare.locale == QStringLiteral("en"));
    QL_CHECK(lightSquare.cardRadius == 0.0 && lightSquare.controlRadius == 0.0);

    const QJsonObject look{
        {QStringLiteral("dark"), false},
        {QStringLiteral("rounded"), true},
        {QStringLiteral("cardRadius"), 12.0},
        {QStringLiteral("controlRadius"), 8.0},
        {QStringLiteral("background"), QStringLiteral("#FFF0F1F2")},
    };
    const ql::ThemeData custom = ql::ThemeData::fromAppearance(
        QStringLiteral("zh"), QStringLiteral("dark"),
        QJsonObject{{QStringLiteral("nativeLook"), look}});
    QL_CHECK(!custom.dark && custom.rounded);
    QL_CHECK(custom.cardRadius == 12.0 && custom.controlRadius == 8.0);
    QL_CHECK(custom.background == QStringLiteral("#FFF0F1F2"));

    ql::ThemeObject obj;
    int changes = 0;
    QObject::connect(&obj, &ql::ThemeObject::changed, [&]() { ++changes; });
    obj.update(lightSquare);
    obj.update(lightSquare);
    QL_CHECK(changes == 1);
}

void testThemeAlphaNormalization()
{
    const QJsonObject winshunLightLook{
        {QStringLiteral("dark"), false},
        {QStringLiteral("rounded"), true},
        {QStringLiteral("page"), QStringLiteral("#E6F3F3F3")},
        {QStringLiteral("layer"), QStringLiteral("#B3FFFFFF")},
        {QStringLiteral("menuBackground"), QStringLiteral("#CCFFFFFF")},
        {QStringLiteral("divider"), QStringLiteral("#14000000")},
        {QStringLiteral("windowEdge"), QStringLiteral("#26000000")},
        {QStringLiteral("hover"), QStringLiteral("#0F000000")},
        {QStringLiteral("pressed"), QStringLiteral("#1A000000")},
        {QStringLiteral("controlHover"), QStringLiteral("#0D000000")},
        {QStringLiteral("menuHover"), QStringLiteral("#12000000")},
    };
    const ql::ThemeData normalized = ql::ThemeData::fromAppearance(
        QStringLiteral("zh"), QStringLiteral("light"),
        QJsonObject{{QStringLiteral("nativeLook"), winshunLightLook}});

    QL_CHECK(!normalized.dark);
    for (const QString& hex : {
             normalized.page, normalized.background, normalized.layer,
             normalized.menuBackground, normalized.control,
             normalized.windowEdge, normalized.divider, normalized.controlBorder,
             normalized.hover, normalized.pressed,
             normalized.controlHover, normalized.menuHover}) {
        const QColor c(hex);
        QL_CHECK(c.isValid() && c.alpha() == 255);
    }
    QL_CHECK(QColor(normalized.windowEdge).red() >= 180);
    QL_CHECK(QColor(normalized.divider).red() >= 200);
    for (const QString& hoverHex : {
             normalized.hover, normalized.pressed,
             normalized.controlHover, normalized.menuHover}) {
        QL_CHECK(QColor(hoverHex).red() >= 200);
    }

    ql::ThemeObject alphaCheckObj;
    QL_CHECK(alphaCheckObj.withAlpha(QColor(QStringLiteral("#14000000")), 0.8).alpha() <= 20);

    const QJsonObject harshLightLook{
        {QStringLiteral("dark"), false},
        {QStringLiteral("page"), QStringLiteral("#FFF8F9FB")},
        {QStringLiteral("windowEdge"), QStringLiteral("#FF101010")},
        {QStringLiteral("divider"), QStringLiteral("#FF202020")},
        {QStringLiteral("controlBorder"), QStringLiteral("#FF151515")},
    };
    const ql::ThemeData softened = ql::ThemeData::fromAppearance(
        QStringLiteral("zh"), QStringLiteral("light"),
        QJsonObject{{QStringLiteral("nativeLook"), harshLightLook}});
    QL_CHECK(QColor(softened.windowEdge).alpha() == 255 && QColor(softened.windowEdge).red() >= 180);
    QL_CHECK(QColor(softened.divider).alpha() == 255 && QColor(softened.divider).red() >= 200);
    QL_CHECK(QColor(softened.controlBorder).alpha() == 255 && QColor(softened.controlBorder).red() >= 180);

    ql::ThemeObject lightObj;
    lightObj.update(normalized);
    QL_CHECK(lightObj.surface() == QColor(QStringLiteral("#FFFFFF")));
    for (const QColor& c : {
             lightObj.surface(), lightObj.sidebarBackground(),
             lightObj.cardSurface(), lightObj.softBorder()}) {
        QL_CHECK(c.isValid() && c.alpha() == 255);
    }

    const ql::ThemeData darkNormalized = ql::ThemeData::fromAppearance(
        QStringLiteral("zh"), QStringLiteral("dark"),
        QJsonObject{{QStringLiteral("nativeLook"), QJsonObject{
            {QStringLiteral("dark"), true},
            {QStringLiteral("page"), QStringLiteral("#E61C1E22")},
            {QStringLiteral("layer"), QStringLiteral("#B32A2C32")},
            {QStringLiteral("menuBackground"), QStringLiteral("#CC25262B")},
        }}});
    QL_CHECK(QColor(darkNormalized.page).alpha() == 255);
    QL_CHECK(QColor(darkNormalized.layer).alpha() == 255);
    QL_CHECK(QColor(darkNormalized.menuBackground).alpha() == 255);

    ql::ThemeObject darkObj;
    darkObj.update(darkNormalized);
    for (const QColor& c : {
             darkObj.surface(), darkObj.sidebarBackground(),
             darkObj.cardSurface(), darkObj.softBorder()}) {
        QL_CHECK(c.isValid() && c.alpha() == 255);
    }
    QL_CHECK(darkObj.surface() != darkObj.layer());
    QL_CHECK(ql::blendOverOpaque(QStringLiteral("#00FFFFFF"), QStringLiteral("#FF112233"))
             == QStringLiteral("#FF112233"));
}

void testMayeNanoImporter()
{
    QTemporaryDir tempDir;
    QL_CHECK(tempDir.isValid());

    ql::LauncherStore store(tempDir.path());
    store.loadOrInitialize();
    QL_CHECK(store.deleteItem(QStringLiteral("def_computer")));
    QL_CHECK(store.deleteItem(QStringLiteral("def_controls")));

    const QString mdbPath = QDir(tempDir.path()).filePath(QStringLiteral("MDB.json"));
    const QJsonObject mdb{
        {QStringLiteral("cate"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("c_exist")}, {QStringLiteral("name"), QStringLiteral("常用")}, {QStringLiteral("order"), 0}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("c_port")}, {QStringLiteral("name"), QStringLiteral("便携工具")}, {QStringLiteral("order"), 1}},
        }},
        {QStringLiteral("item"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m1")}, {QStringLiteral("idCate"), QStringLiteral("c_port")},
                        {QStringLiteral("name"), QStringLiteral("工具A")}, {QStringLiteral("uri"), QStringLiteral("%mp%\\bin\\app.exe")},
                        {QStringLiteral("param"), QStringLiteral("--root=%mr%")}, {QStringLiteral("dir"), QStringLiteral("%mp%\\bin")}, {QStringLiteral("order"), 0}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m2")}, {QStringLiteral("idCate"), QStringLiteral("c_exist")},
                        {QStringLiteral("name"), QStringLiteral("系统记事本")}, {QStringLiteral("uri"), QStringLiteral("%windir%\\notepad.exe")},
                        {QStringLiteral("dir"), QStringLiteral("%windir%")}, {QStringLiteral("order"), 1}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m3")}, {QStringLiteral("idCate"), QStringLiteral("c_exist")},
                        {QStringLiteral("name"), QStringLiteral("我的电脑")}, {QStringLiteral("uri"), QStringLiteral("$computer")}, {QStringLiteral("order"), 2}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m4")}, {QStringLiteral("idCate"), QStringLiteral("c_exist")},
                        {QStringLiteral("name"), QStringLiteral("控制面板")}, {QStringLiteral("uri"), QStringLiteral("$controls")}, {QStringLiteral("order"), 3}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m5")}, {QStringLiteral("idCate"), QStringLiteral("c_port")},
                        {QStringLiteral("name"), QStringLiteral("空容器")}, {QStringLiteral("uri"), QString()}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("m6")}, {QStringLiteral("idCate"), QStringLiteral("c_port")},
                        {QStringLiteral("name"), QStringLiteral("虚拟组")}, {QStringLiteral("uri"), QStringLiteral("virtual:folder")}, {QStringLiteral("type"), QStringLiteral("virtual")}},
        }},
    };
    {
        QFile f(mdbPath);
        QL_CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(mdb).toJson(QJsonDocument::Indented));
    }

    const ql::MayeImportResult r1 = ql::MayeNanoImporter::importFromFile(mdbPath, store);
    QL_CHECK(r1.addedCategories == 1);
    QL_CHECK(r1.addedItems == 4);
    QL_CHECK(r1.warnings.size() >= 2);

    const ql::LauncherDatabase snap = store.snapshot();
    bool sawApp = false, sawSys = false, sawComp = false, sawCtrl = false;
    const QString expectedMp = QDir::toNativeSeparators(QFileInfo(mdbPath).absolutePath());
    for (const auto& it : snap.items) {
        if (it.name == QStringLiteral("工具A")) {
            sawApp = it.target.startsWith(expectedMp) && it.workingDir.startsWith(expectedMp)
                     && !it.arguments.contains(QStringLiteral("%mr%"), Qt::CaseInsensitive);
        } else if (it.name == QStringLiteral("系统记事本")) {
            sawSys = it.target.compare(QStringLiteral("%windir%\\notepad.exe"), Qt::CaseInsensitive) == 0;
        } else if (it.target == QStringLiteral("$computer")) {
            sawComp = true;
        } else if (it.target == QStringLiteral("$controls")) {
            sawCtrl = true;
        }
    }
    QL_CHECK(sawApp && sawSys && sawComp && sawCtrl);

    const ql::MayeImportResult r2 = ql::MayeNanoImporter::importFromFile(mdbPath, store);
    QL_CHECK(r2.addedCategories == 0 && r2.addedItems == 0 && r2.skippedDuplicates == 4);

    const ql::MayeImportResult missing = ql::MayeNanoImporter::importFromFile(
        QDir(tempDir.path()).filePath(QStringLiteral("missing.json")), store);
    QL_CHECK(missing.addedItems == 0 && !missing.warnings.isEmpty());

    const QString badPath = QDir(tempDir.path()).filePath(QStringLiteral("bad.json"));
    {
        QFile bf(badPath);
        QL_CHECK(bf.open(QIODevice::WriteOnly | QIODevice::Truncate));
        bf.write("not-json");
    }
    const ql::MayeImportResult corrupt = ql::MayeNanoImporter::importFromFile(badPath, store);
    QL_CHECK(corrupt.addedItems == 0 && !corrupt.warnings.isEmpty());
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testLauncherStore();
    testPinyinMatcher();
    testMayeNanoImporter();
    testShellBridge();
    testThemeData();
    testThemeAlphaNormalization();
    if (g_failures > 0) {
        std::cerr << "[CoreTests] FAILED: " << g_failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "[CoreTests] ALL PASSED\n";
    return EXIT_SUCCESS;
}
