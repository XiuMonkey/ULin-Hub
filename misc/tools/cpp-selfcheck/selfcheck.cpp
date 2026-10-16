// 搬家后的真 C++ 自检（不依赖 QML/引擎）：直接链接被测的 4 个 .cpp。
// 覆盖：① UrlInput 规范化 ② PanelDockModel 的 open/close/activate/showHome + 视图表（kind → 视图）
//       ③ PanelBarMetrics 的钳制/拖动公式（common/layout，自由函数）+ PanelBarLayout 转发层是否逐位一致。
//       ④【本次新增】PanelDockModel 的「块脱离成独立窗口」状态机（脱离/回附/关独立窗口/几何回写）。
// 两个被测类都是"程序级唯一实例、刻意不析构"的单例（QML 单例注册要求活过引擎）⇒ 用 instance()。
//
// 路径已随"领域归位 2026-10-15"更新：模型搬去 common/**（PanelDockModel → common/shell），
// 右栏宽度公式抽成 common/layout/PanelBarMetrics（自由函数），qmlbridge/PanelBarLayout 只剩转发。
// 这正是能把它们搬出 QML 的理由：断言只需要 QtCore，不需要 QML 引擎。

#include "common/layout/PanelBarMetrics.h"
#include "common/shell/PanelDockModel.h"
#include "common/util/UrlInput.h"
#include "qmlbridge/PanelBarLayout.h"

#include <QCoreApplication>
#include <QHash>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <cstdio>

static int g_fail = 0;

static void check(bool ok, const QString& what, const QString& detail = QString())
{
	const QByteArray w = what.toUtf8();
	const QByteArray d = detail.toUtf8();
	std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", w.constData(), d.isEmpty() ? "" : "   [",
	            d.isEmpty() ? "" : d.constData());
	if (!d.isEmpty())
		std::printf("%s\n", ok ? "" : "");
	if (!ok)
		++g_fail;
}

static void urlCases()
{
	std::printf("\n=== ① UrlInput::normalizeUrlInput ===\n");
	const QStringList inputs = { QStringLiteral("a.com"),
		                         QStringLiteral("http://x/y"),
		                         QStringLiteral("localhost:8080"),
		                         QStringLiteral("你好 世界"),
		                         QStringLiteral("a b.com"),
		                         QStringLiteral("https://例子.测试/路径?q=1"),
		                         QStringLiteral(""),
		                         QStringLiteral("   ") };
	for (const QString& in : inputs)
		std::printf("    %-34s -> %s\n", ("\"" + in + "\"").toUtf8().constData(),
		            normalizeUrlInput(in).toUtf8().constData());

	check(normalizeUrlInput(QStringLiteral("")) == QString(), QStringLiteral("空串 -> 空串（调用方什么都不做）"));
	check(normalizeUrlInput(QStringLiteral("   ")) == QString(), QStringLiteral("纯空白 -> 空串"));
	check(normalizeUrlInput(QStringLiteral("http://x/y")) == QStringLiteral("http://x/y"),
	      QStringLiteral("已带 scheme 原样返回"));
	check(normalizeUrlInput(QStringLiteral("a.com")).startsWith(QStringLiteral("https://")),
	      QStringLiteral("像域名的补 https://"));
	check(normalizeUrlInput(QStringLiteral("a b.com")).contains(QStringLiteral("bing.com/search?q=")),
	      QStringLiteral("不像域名的走搜索"));
	check(normalizeUrlInput(QStringLiteral("你好 世界")).contains(QStringLiteral("%E4%BD%A0%E5%A5%BD")),
	      QStringLiteral("中文按 UTF-8 百分号编码"));
}

static void modelCases()
{
	std::printf("\n=== ② PanelDockModel（common/shell）===\n");
	PanelDockModel& model = PanelDockModel::instance();
	const auto title = [&model](int row) {
		return model.data(model.index(row, 0), PanelDockModel::TitleRole).toString();
	};
	const auto count = [&model] { return model.rowCount(); };

	model.showHome();
	model.open(QStringLiteral("browser"));
	model.open(QStringLiteral("browser"));
	std::printf("    open browser x2 -> count=%d  titles=[%s, %s]  activeIndex=%d  activeTitle=%s\n", count(),
	            title(0).toUtf8().constData(), title(1).toUtf8().constData(), model.activeIndex(),
	            model.activeTitle().toUtf8().constData());
	check(count() == 2, QStringLiteral("open 两次 -> count=2"), QString::number(count()));
	check(title(0) != title(1), QStringLiteral("同类块标题编号不同"), title(0) + QStringLiteral(" / ") + title(1));
	check(model.activeIndex() == 1, QStringLiteral("open 后激活新块"), QString::number(model.activeIndex()));

	model.close(0);
	std::printf("    close(0) -> count=%d  titles=[%s]  activeIndex=%d\n", count(),
	            title(0).toUtf8().constData(), model.activeIndex());
	check(count() == 1, QStringLiteral("close 后 count=1"), QString::number(count()));
	check(model.activeIndex() >= 0 && model.activeIndex() < count(),
	      QStringLiteral("close 后 activeIndex 收敛到有效值"), QString::number(model.activeIndex()));

	model.close(0);
	std::printf("    close(0) -> count=%d  homeVisible=%d\n", count(), model.homeVisible());
	check(count() == 0 && model.homeVisible(), QStringLiteral("关光了 -> homeVisible=true"));

	model.open(QStringLiteral("files"));
	model.showHome();
	std::printf("    open files + showHome -> count=%d  homeVisible=%d\n", count(), model.homeVisible());
	check(count() == 1 && model.homeVisible(), QStringLiteral("⌂ 回主界面：homeVisible=true 且块保留"));

	model.close(0);   // 收尾：把单例还原成"空 + 主界面"
	check(PanelDockModel::kindLabel(QStringLiteral("browser")) == QStringLiteral("网页浏览"),
	      QStringLiteral("中文块名在 C++ 侧"), PanelDockModel::kindLabel(QStringLiteral("browser")));

	// 视图表（kind → 视图）：路由的映射规则在 C++，QML 只按这组值装机，不再出现 kind 字符串
	const PanelDockModel::ViewSpec browserView = PanelDockModel::viewFor(QStringLiteral("browser"), true);
	const PanelDockModel::ViewSpec terminalView = PanelDockModel::viewFor(QStringLiteral("terminal"), true);
	const PanelDockModel::ViewSpec filesView = PanelDockModel::viewFor(QStringLiteral("files"), true);
	std::printf("    viewFor -> browser=[%s blockScoped=%d]  terminal=[%s blockScoped=%d]\n",
	            browserView.path.toUtf8().constData(), browserView.blockScoped,
	            terminalView.path.toUtf8().constData(), terminalView.blockScoped);
	check(browserView.path == QStringLiteral("basic/browser/WebBrowserPanel.qml") && !browserView.blockScoped,
	      QStringLiteral("browser 视图 = WebBrowserPanel，不要当前块"), browserView.path);
	check(terminalView.path == QStringLiteral("basic/terminal/TerminalPanel.qml") && terminalView.blockScoped,
	      QStringLiteral("terminal 视图 = TerminalPanel，要当前块"), terminalView.path);
	check(filesView.path.isEmpty() && !filesView.detail.isEmpty(),
	      QStringLiteral("files 没有视图 -> 走说明文案"), filesView.detail);

	// 缺 WebEngine：只有依赖它的两个块降级成说明页（不能去建 WebEngineView），其余不受影响
	const PanelDockModel::ViewSpec browserNoEngine = PanelDockModel::viewFor(QStringLiteral("browser"), false);
	check(browserNoEngine.path.isEmpty() && browserNoEngine.detail.contains(QStringLiteral("WebEngine")),
	      QStringLiteral("缺 WebEngine -> browser 降级成说明页"), browserNoEngine.detail);
	check(PanelDockModel::viewFor(QStringLiteral("files"), false).path == filesView.path,
	      QStringLiteral("缺 WebEngine 不影响 files"), filesView.path);

	// 关掉活动块**前面**那块：索引不动但活动块换成原来的下一块（kind 也可能换）⇒ 视图那组值
	// 必须一起通知。没有 activeViewChanged，QML 路由会停在旧视图上（这条是实测出来的坑）。
	int viewSignals = 0;
	QObject::connect(&model, &PanelDockModel::activeViewChanged, [&viewSignals] { ++viewSignals; });
	model.showHome();
	model.open(QStringLiteral("terminal"));   // [T]
	model.open(QStringLiteral("browser"));    // [T, B]   active=1
	model.open(QStringLiteral("terminal"));   // [T, B, T] active=2
	model.activate(1);                        // active=1 (B)
	viewSignals = 0;
	model.close(0);                           // [B, T] 索引仍为 1 ⇒ 活动块换成 T（kind 变了）
	std::printf("    close(before active) -> activeKind=%s  activeViewPath=%s  activeViewChanged=%d\n",
	            model.activeKind().toUtf8().constData(), model.activeViewPath().toUtf8().constData(),
	            viewSignals);
	check(viewSignals > 0, QStringLiteral("关前一块 -> activeViewChanged 有通知"), QString::number(viewSignals));
	check(model.activeKind() == QStringLiteral("terminal") && model.activeViewPath() == terminalView.path,
	      QStringLiteral("关前一块后活动块换成 T，视图跟着换"), model.activeViewPath());

	model.close(0);   // 收尾：还原成"空 + 主界面"
	model.close(0);
	model.showHome();
}

static void sidebarCases()
{
	std::printf("\n=== ③ PanelBarMetrics（common/layout，自由函数）+ PanelBarLayout 转发层 ===\n");

	// ---- 算法本体：自由函数，没有 QObject、不碰 QML ----
	const double convMin = PanelBarMetrics::conversationMinWidth(1232.0, 308.0);
	const double maxW = PanelBarMetrics::maxWidth(1232.0, 260.0, convMin, 12.0, 311.0, 12.0);
	const double dragLeft = PanelBarMetrics::widthFromDrag(100.0, 300.0, 50.0, 260.0, maxW);
	const double dragRight = PanelBarMetrics::widthFromDrag(100.0, 300.0, 500.0, 260.0, maxW);
	std::printf("    [PanelBarMetrics] convMin(1232,308)=%.1f  maxW=%.1f  dragL(50)=%.1f  dragR(500)=%.1f\n",
	            convMin, maxW, dragLeft, dragRight);
	check(qFuzzyCompare(convMin, 308.0), QStringLiteral("对话区保底 = min(token, 窗口*0.25)"), QString::number(convMin));
	check(maxW >= 260.0, QStringLiteral("上限不低于最小宽度"), QString::number(maxW));
	check(qFuzzyCompare(dragLeft, 350.0), QStringLiteral("往左拖 50 ⇒ 300->350（1:1）"), QString::number(dragLeft));
	check(qFuzzyCompare(dragRight, 260.0), QStringLiteral("往右拖过头 ⇒ 夹在下限 260"), QString::number(dragRight));
	check(qFuzzyCompare(PanelBarMetrics::clampWidth(10.0, 260.0, maxW), 260.0), QStringLiteral("低于下限夹到 min"));
	check(qFuzzyCompare(PanelBarMetrics::clampWidth(9999.0, 260.0, maxW), maxW), QStringLiteral("超过上限夹到 max"));

	// ---- QML 契约面：单例只转发 —— 与自由函数逐位比对（转发层少传/串了参数，这里会红）----
	PanelBarLayout& layout = PanelBarLayout::instance();
	const double bConvMin = layout.conversationMinWidth(1232.0, 308.0);
	const double bMaxW = layout.maxWidth(1232.0, 260.0, bConvMin, 12.0, 311.0, 12.0);
	const double bDragLeft = layout.widthFromDrag(100.0, 300.0, 50.0, 260.0, bMaxW);
	const double bClamp = layout.clampWidth(9999.0, 260.0, bMaxW);
	std::printf("    [PanelBarLayout 转发] convMin=%.1f  maxW=%.1f  dragL=%.1f  clamp(9999)=%.1f\n",
	            bConvMin, bMaxW, bDragLeft, bClamp);
	check(qFuzzyCompare(bConvMin, convMin) && qFuzzyCompare(bMaxW, maxW)
	          && qFuzzyCompare(bDragLeft, dragLeft) && qFuzzyCompare(bClamp, maxW),
	      QStringLiteral("单例与 PanelBarMetrics 逐位一致（qmlbridge 只转发）"));
}

// ④ 面板坞「块脱离成独立窗口」的状态机（**本次新增**，2026-10-15）：
//    脱离/回附/关独立窗口/几何回写都是能被字符串与数字独立描述的东西 ⇒ 按本工具的一贯判据
//    （能脱离 QML 引擎断言的，就该在 common/** 里、就该在这儿有断言）放在这里，不靠 GUI 取证。
//    ⚠️ 只断言**模型那一侧的账**：窗口对象与系统装饰（四边缩放、最小化/最大化按钮）是 QML/Qt 的事，
//    这里证不了，别把它的通过当成"功能可用"。
static void detachCases()
{
	std::printf("\n=== ④ PanelDockModel 的「脱离成独立窗口」状态机 ===\n");
	PanelDockModel& model = PanelDockModel::instance();
	const auto count = [&model] { return model.rowCount(); };
	// 按稳定 id 在 detachedBlocks 里找那一项（找不到返回空 map）
	const auto entry = [&model](const QString& id) {
		for (const QVariant& v : model.detachedBlocks()) {
			const QVariantMap m = v.toMap();
			if (m.value(QStringLiteral("blockId")).toString() == id)
				return m;
		}
		return QVariantMap();
	};

	model.showHome();
	model.open(QStringLiteral("browser"));
	const QString browserId = model.activeBlockId();
	model.open(QStringLiteral("terminal"));
	const QString terminalId = model.activeBlockId();   // 活动块是最后开的这块
	std::printf("    open browser+terminal -> count=%d  ids=[%s, %s]  active=%s\n", count(),
	            browserId.toUtf8().constData(), terminalId.toUtf8().constData(),
	            model.activeKind().toUtf8().constData());

	// 脱离第 0 块（browser）：脱离 + 窗口显示 ⇒ 顶栏 ◫ 的"已独立"那一态。
	// ⚠️ 视图路径那两条断言必须在"WebEngine 可用"下做（browser/terminal 的视图依赖它），
	//    否则拿到的是说明页 —— 那是另一条**正确**行为，不是 bug（见下面缺 WebEngine 的一小段）。
	const bool engineBackup = model.webEngineAvailable();
	model.setWebEngineAvailable(true);

	model.detach(0);
	QVariantMap de = entry(browserId);
	std::printf("    detach(0) -> detachedBlocks=%d  visible=%d  viewPath=%s\n", model.detachedBlocks().size(),
	            de.value(QStringLiteral("visible")).toBool(),
	            de.value(QStringLiteral("viewPath")).toString().toUtf8().constData());
	check(model.detachedBlocks().size() == 1 && de.value(QStringLiteral("visible")).toBool(),
	      QStringLiteral("脱离后出现在 detachedBlocks 里且窗口显示中"));
	check(de.value(QStringLiteral("viewPath")).toString() == QStringLiteral("basic/browser/WebBrowserPanel.qml"),
	      QStringLiteral("独立窗口的视图路径按**该块自己的 kind** 解析（不是活动块那组）"),
	      de.value(QStringLiteral("viewPath")).toString());
	check(de.value(QStringLiteral("viewDetail")).toString().isEmpty(),
	      QStringLiteral("WebEngine 可用时照常给视图、不降级"));
	check(model.anyDetachedVisible(), QStringLiteral("anyDetachedVisible=true（◫ 走\"关独立窗口\"那一态）"));

	// 缺 WebEngine 时独立窗口同样降级成说明页（判定权在 C++ 的视图表，同一条路）
	model.setWebEngineAvailable(false);
	de = entry(browserId);
	check(de.value(QStringLiteral("viewPath")).toString().isEmpty()
	          && de.value(QStringLiteral("viewDetail")).toString().contains(QStringLiteral("WebEngine")),
	      QStringLiteral("缺 WebEngine -> 独立窗口照样是说明页（不去建 WebEngineView）"),
	      de.value(QStringLiteral("viewDetail")).toString());
	model.setWebEngineAvailable(engineBackup);

	// 几何回写 + 同值不发通知（QML 侧窗口的 x/y/宽/高靠"同值不发"防自激）
	int geomSignals = 0;
	QObject::connect(&model, &PanelDockModel::detachedChanged, [&geomSignals] { ++geomSignals; });
	model.setDetachedGeometry(browserId, 111, 222, 800, 600);
	geomSignals = 0;
	model.setDetachedGeometry(browserId, 111, 222, 800, 600);
	de = entry(browserId);
	std::printf("    geometry -> x=%d y=%d w=%d h=%d  (同值再写一次的通知数=%d)\n",
	            de.value(QStringLiteral("x")).toInt(), de.value(QStringLiteral("y")).toInt(),
	            de.value(QStringLiteral("w")).toInt(), de.value(QStringLiteral("h")).toInt(), geomSignals);
	check(de.value(QStringLiteral("x")).toInt() == 111 && de.value(QStringLiteral("w")).toInt() == 800,
	      QStringLiteral("几何回写进模型（脱离/回附来回走也不丢）"));
	check(geomSignals == 0, QStringLiteral("几何同值不发通知（否则窗口会被自己绑的几何抖回去）"),
	      QString::number(geomSignals));

	// 顶栏 ◫ 的"已独立"那一态：关掉所有独立窗口 ⇒ 块回到"已附着但隐藏"（脱离状态与几何都留着）
	check(model.closeDetachedWindows() == 1 && !model.anyDetachedVisible(),
	      QStringLiteral("closeDetachedWindows 关掉那一扇（◫ 三态里的第二态）"));
	de = entry(browserId);
	check(!de.isEmpty() && !de.value(QStringLiteral("visible")).toBool()
	          && de.value(QStringLiteral("x")).toInt() == 111,
	      QStringLiteral("关掉后仍是\"已附着但隐藏\"：脱离状态与几何都还在"));

	// showHome 不该把脱离状态清掉（块没被关，只是坞里显示了启动器）
	model.showHome();
	check(model.detachedBlocks().size() == 1, QStringLiteral("⌂ 回主界面不影响块的脱离状态"));

	// 回附：脱离状态清掉（窗口随之销毁）+ 该块被激活
	model.attach(browserId);
	std::printf("    attach(%s) -> detachedBlocks=%d  activeId=%s  anyVisible=%d\n",
	            browserId.toUtf8().constData(), model.detachedBlocks().size(),
	            model.activeBlockId().toUtf8().constData(), model.anyDetachedVisible());
	check(model.detachedBlocks().isEmpty() && !model.anyDetachedVisible(),
	      QStringLiteral("回附后脱离状态清掉（独立窗口随之销毁）"));
	check(model.activeBlockId() == browserId && !model.homeVisible(),
	      QStringLiteral("回附的块被激活、离开主界面（回到它原来待的地方）"));

	model.close(0);
	model.close(0);
	model.showHome();
	check(count() == 0, QStringLiteral("收尾：块表清空还原单例"), QString::number(count()));
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	urlCases();
	modelCases();
	sidebarCases();
	detachCases();
	std::printf("\n=== 合计：%s（失败 %d 项）===\n", g_fail == 0 ? "全部 PASS" : "有 FAIL", g_fail);
	return g_fail == 0 ? 0 : 1;
}
