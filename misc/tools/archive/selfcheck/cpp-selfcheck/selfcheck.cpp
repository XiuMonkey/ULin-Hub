// 搬家后的真 C++ 自检（不依赖 QML/引擎）：直接链接被测的 .cpp。
// 覆盖：① UrlInput 规范化 ② PanelBarDockModel 的 open/close/activate/showHome + 视图表（kind → 视图）
//       ③ CodeHighlighter 的规则装载 + 别名归一。
// 两个被测类都是"程序级唯一实例、刻意不析构"的单例（QML 单例注册要求活过引擎）⇒ 用 instance()。
//
// 路径已随"领域归位 2026-10-15"更新：模型搬去 common/**（PanelBarDockModel → common/shell）。
// 右侧栏宽度公式**不在本自检范围内**：2026-10-18 已归位到 QML（见下方 ③ 之前的说明）。

#include "common/util/UrlInput.h"
#include "common/shell/PanelBarDockModel.h"
#include "common/util/CodeHighlighter.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
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
	std::printf("\n=== ② PanelBarDockModel（common/shell）===\n");
	PanelBarDockModel& model = PanelBarDockModel::instance();
	// 自检模拟的是"带 WebEngine 的本构建"：main.cpp 启动时就是这么注入的。
	// ⚠️ 少了这一行，viewFor 会把 browser/terminal 都降级成说明页，于是
	//    "关前一块后活动块换成 T，视图跟着换"那条断言拿空 path 去比期望值 —— 恒 FAIL。
	model.setWebEngineAvailable(true);
	const auto title = [&model](int row) {
		return model.data(model.index(row, 0), PanelBarDockModel::TitleRole).toString();
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
	check(PanelBarDockModel::kindLabel(QStringLiteral("browser")) == QStringLiteral("网页浏览"),
	      QStringLiteral("中文块名在 C++ 侧"), PanelBarDockModel::kindLabel(QStringLiteral("browser")));

	// 视图表（kind → 视图）：路由的映射规则在 C++，QML 只按这组值装机，不再出现 kind 字符串
	const PanelBarDockModel::ViewSpec browserView = PanelBarDockModel::viewFor(QStringLiteral("browser"), true);
	const PanelBarDockModel::ViewSpec terminalView = PanelBarDockModel::viewFor(QStringLiteral("terminal"), true);
	const PanelBarDockModel::ViewSpec filesView = PanelBarDockModel::viewFor(QStringLiteral("files"), true);
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
	const PanelBarDockModel::ViewSpec browserNoEngine = PanelBarDockModel::viewFor(QStringLiteral("browser"), false);
	check(browserNoEngine.path.isEmpty() && browserNoEngine.detail.contains(QStringLiteral("WebEngine")),
	      QStringLiteral("缺 WebEngine -> browser 降级成说明页"), browserNoEngine.detail);
	check(PanelBarDockModel::viewFor(QStringLiteral("files"), false).path == filesView.path,
	      QStringLiteral("缺 WebEngine 不影响 files"), filesView.path);

	// 关掉活动块**前面**那块：索引不动但活动块换成原来的下一块（kind 也可能换）⇒ 视图那组值
	// 必须一起通知。没有 activeViewChanged，QML 路由会停在旧视图上（这条是实测出来的坑）。
	int viewSignals = 0;
	QObject::connect(&model, &PanelBarDockModel::activeViewChanged, [&viewSignals] { ++viewSignals; });
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

	// ---- 插件界面块（启动器第四行）：与另外三个块同形，视图是浏览器面板，地址来自 pluginPageUrl ----
	const QVariantList rows = model.launcherItems();
	QStringList rowKinds;
	for (const QVariant& row : rows)
		rowKinds.append(row.toMap().value(QStringLiteral("kind")).toString());
	check(rows.size() == 4 && rowKinds.contains(QStringLiteral("pluginUi")),
	      QStringLiteral("启动器有四行且含插件界面"), rowKinds.join(QStringLiteral(",")));
	const PanelBarDockModel::ViewSpec puView = PanelBarDockModel::viewFor(QStringLiteral("pluginUi"), true);
	check(puView.path == QStringLiteral("basic/panelui/PluginUiPanel.qml"),
	      QStringLiteral("插件界面块是原生面板（不是网页）"), puView.path);

	// 原生面板不吃 pluginPageUrl：页面由面板里的按钮另开一块 browser 块
	model.setPluginPageUrl(QStringLiteral("http://127.0.0.1:9/plugin/"));
	model.open(QStringLiteral("pluginUi"));
	check(model.activeViewUrl().isEmpty(),
	      QStringLiteral("原生插件界面面板不吃 pluginPageUrl"), model.activeViewUrl());
	model.close(0);
	model.open(QStringLiteral("browser"));
	check(model.activeViewUrl().isEmpty(),
	      QStringLiteral("用户开的浏览器块也不受 pluginPageUrl 影响"), model.activeViewUrl());
	model.close(0);
	model.setPluginPageUrl(QString());
	// ---- 带初始地址的块：openUrl 返回块 id / closeBlock 按 id 关 ----
	// 语义：与 open("browser") 同一条路径，只多记一个初始地址并返回 id；模型不记"是谁开的"。
	while (count() > 0)                      // 上面那组用例留下的块先清空，下面的绝对计数才好写
		model.close(0);
	model.showHome();
	model.open(QStringLiteral("browser"));   // [U] 用户自己开的块（无初始地址）
	const QString extAUrl = QStringLiteral("http://127.0.0.1:53232/plugin-ui/a/");
	const QString extBUrl = QStringLiteral("http://127.0.0.1:53232/plugin-ui/b/");

	const QString idA = model.openUrl(extAUrl, QStringLiteral("插件 A"));
	check(!idA.isEmpty(), QStringLiteral("openUrl 返回块 id"), idA);
	check(model.activeKind() == QStringLiteral("browser") && model.activeViewPath() == browserView.path,
	      QStringLiteral("带地址的块就是 browser 块（不新增 kind）"), model.activeKind());
	check(model.activeBlockId() == idA, QStringLiteral("返回的 id 就是活动块 id"), model.activeBlockId());
	check(model.activeViewUrl() == extAUrl, QStringLiteral("activeViewUrl 跟着活动块"), model.activeViewUrl());
	check(title(count() - 1) == QStringLiteral("插件 A"),
	      QStringLiteral("给了标题就用它"), title(count() - 1));

	check(model.openUrl(QString(), QStringLiteral("x")).isEmpty(),
	      QStringLiteral("空 url 拒绝（开出来是空面板）"));
	check(count() == 2, QStringLiteral("拒绝时不追加块"), QString::number(count()));

	viewSignals = 0;
	const QString idB = model.openUrl(extBUrl, QString());
	check(viewSignals == 1, QStringLiteral("openUrl 与 open 同一节奏：activeViewChanged 只发 1 次"),
	      QString::number(viewSignals));
	check(title(count() - 1).startsWith(QStringLiteral("网页浏览")),
	      QStringLiteral("空标题回落到内置块名 + 编号"), title(count() - 1));

	const QStringList ids = model.blockIds();
	check(ids.size() == 3 && ids.contains(idA) && ids.contains(idB),
	      QStringLiteral("blockIds 按打开顺序给出全部 id"), ids.join(QStringLiteral(",")));

	check(model.closeBlock(idA) && count() == 2, QStringLiteral("closeBlock 按 id 关"),
	      QString::number(count()));
	check(!model.closeBlock(idA), QStringLiteral("已关的 id 再关 = false（幂等）"));
	check(model.activeViewUrl() == extBUrl,
	      QStringLiteral("关掉前面那块后 activeViewUrl 跟着换（不串台）"), model.activeViewUrl());

	check(model.closeBlock(idB) && count() == 1, QStringLiteral("关光后只剩用户块"), QString::number(count()));
	check(model.activeViewUrl().isEmpty(),
	      QStringLiteral("用户块的 activeViewUrl 恒为空（面板显示空态）"), model.activeViewUrl());

	model.close(0);   // 收尾：还原成"空 + 主界面"
	model.showHome();
}

// ③ 已归位到 QML（2026-10-18）：右侧栏宽度那三条公式（会话区保底 / 上限 / 夹取）连同拖动换算
//    搬回了 QML —— 它们唯一的消费者就是 Shell.qml，C++ 侧一个调用者都没有。
//    现位置：source/qml/basic/shell/Shell.qml 的 conversationMinWidth / maxWidth / clampWidth
//    （+ 壳层包装 widthFromDrag / setPanelBarWidth）。原来这里断言的 350、260、308 那几条只覆盖
//    "min/max 组合是否写反"，真正的风险点（拖柄喂的是 panelBarEffW 还是 panelBarW、冻结位置的时序）
//    一条也没覆盖，所以不另建 QML 侧测试基座，改为在那边靠真机跟手手感验收。
//    ⚠️ 别为了"有断言"再把这几式搬回 C++：那会重新长出 common/layout + qmlbridge 转发层两套壳。

// 读主题包里的字符串键（颜色都是字符串值）
static QHash<QString, QString> loadPalette(const QString& path)
{
	QHash<QString, QString> out;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return out;
	const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
	for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
		if (it.value().isString())
			out.insert(it.key(), it.value().toString());
	}
	return out;
}

// ③ 语法高亮：规则文件的 PCRE2 兼容性 + 别名归一 + 几个"曾经踩过"的回归点。
//    这些断言只依赖 QtCore（QRegularExpression 就是引擎本身），所以能放进这个纯 QtCore 自检里。
static void highlightCases(const QString& repoRoot)
{
	std::printf("\n=== ③ CodeHighlighter（语法高亮规则 + 别名归一） ===\n");

	const QString rulesPath = repoRoot + QStringLiteral("/assets/resources/config/highlight-rules.json");
	CodeHighlighter& hl = CodeHighlighter::instance();
	check(hl.loadFromFile(rulesPath), QStringLiteral("规则文件装载"), rulesPath);
	check(hl.loadFromFile(rulesPath), QStringLiteral("重复装载不崩（先清空再读）"));

	const auto hasSpan = [](const QString& html) { return html.contains(QLatin1String("<span style=\"color:")); };

	// ---- 亮/暗两套语法色都来自主题包（规则里只写角色）----
	const QHash<QString, QString> light = loadPalette(repoRoot + QStringLiteral("/assets/resources/styles/theme-light.json"));
	const QHash<QString, QString> dark = loadPalette(repoRoot + QStringLiteral("/assets/resources/styles/theme-dark.json"));
	check(!light.isEmpty() && !dark.isEmpty(), QStringLiteral("两个主题包都可读"));
	check(light.contains(QStringLiteral("syntaxKeyword")) && dark.contains(QStringLiteral("syntaxKeyword")),
	      QStringLiteral("主题包里有 syntax* 键"));
	check(light.value(QStringLiteral("syntaxKeyword")) != dark.value(QStringLiteral("syntaxKeyword")),
	      QStringLiteral("亮/暗语法关键字色不同"),
	      light.value(QStringLiteral("syntaxKeyword")) + QStringLiteral(" vs ") + dark.value(QStringLiteral("syntaxKeyword")));

	// 样例必须同时含"关键字 / 注释 / 内建"三类，否则断言取不到对应角色的色
	const QString pyCode = QStringLiteral("# note\ndef f(x):\n    return print(len(x))\n");
	hl.setThemeColors(light, false);
	const QString lightHtml = hl.highlight(QStringLiteral("python"), pyCode);
	check(lightHtml.contains(light.value(QStringLiteral("syntaxKeyword"))),
	      QStringLiteral("亮色：关键字取亮色主题的 syntaxKeyword"), light.value(QStringLiteral("syntaxKeyword")));
	check(lightHtml.contains(light.value(QStringLiteral("syntaxComment"))),
	      QStringLiteral("亮色：注释取亮色主题的 syntaxComment"));

	hl.setThemeColors(dark, true);
	const QString darkHtml = hl.highlight(QStringLiteral("python"), pyCode);
	check(darkHtml.contains(dark.value(QStringLiteral("syntaxKeyword"))),
	      QStringLiteral("暗色：关键字取暗色主题的 syntaxKeyword"), dark.value(QStringLiteral("syntaxKeyword")));
	check(darkHtml != lightHtml, QStringLiteral("换肤后同一段代码产出不同 HTML（缓存随主题失效）"));
	check(!darkHtml.contains(light.value(QStringLiteral("syntaxKeyword"))),
	      QStringLiteral("暗色结果里不含亮色色值"));

	// 别名：fence 标签的写法千奇百怪，归一后必须落到同一套规则上
	const QString pyCanonical = hl.highlight(QStringLiteral("python"), pyCode);
	check(hasSpan(pyCanonical), QStringLiteral("python 有高亮"));
	check(hl.highlight(QStringLiteral("py"), pyCode) == pyCanonical, QStringLiteral("别名 py -> python"));
	check(hl.highlight(QStringLiteral("PY"), pyCode) == pyCanonical, QStringLiteral("别名大小写不敏感 PY -> python"));
	check(hl.highlight(QStringLiteral("python title=demo"), pyCode) == pyCanonical,
	      QStringLiteral("fence 带附加信息（只取第一个词）"));
	check(hl.highlight(QStringLiteral("c++"), QStringLiteral("std::vector<int> v;"))
	          == hl.highlight(QStringLiteral("cpp"), QStringLiteral("std::vector<int> v;")),
	      QStringLiteral("别名 c++ -> cpp"));
	check(hl.highlight(QStringLiteral("js"), QStringLiteral("const a = 1;"))
	          == hl.highlight(QStringLiteral("typescript"), QStringLiteral("const a = 1;")),
	      QStringLiteral("别名 js -> typescript（与官方一致：JS 家族共用 TS 语法）"));

	// 回归：`<...>` 只该在 #include 里当文件头，不能把 std::vector<int> 的模板实参染成字符串
	const QString cppHtml = hl.highlight(QStringLiteral("cpp"),
		QStringLiteral("#include <vector>\nstd::vector<int> v;\n"));
	check(cppHtml.contains(dark.value(QStringLiteral("syntaxKeyword"))),
	      QStringLiteral("#include 走关键字色（预处理指令归 keyword）"));
	check(!cppHtml.contains(dark.value(QStringLiteral("syntaxString"))),
	      QStringLiteral("模板实参 <int> 不再被当成头文件字符串"));

	// 未知语言 / 空语言：退回 default 规则，绝不抛、绝不空转
	check(hasSpan(hl.highlight(QStringLiteral("no-such-lang"), QStringLiteral("x = 42"))),
	      QStringLiteral("未知语言退回 default（仍有基础高亮）"));
	check(hl.highlight(QString(), QStringLiteral("x = 42")) == hl.highlight(QStringLiteral("text"),
		QStringLiteral("x = 42")), QStringLiteral("空语言 == text"));

	// 高亮结果是"转义过的"，代码里的 < > & 不能原样进 RichText
	const QString escaped = hl.highlight(QStringLiteral("cpp"), QStringLiteral("if (a < b && c > d) {}"));
	check(escaped.contains(QLatin1String("&lt;")) && escaped.contains(QLatin1String("&amp;")),
	      QStringLiteral("代码里的 < > & 被转义"));
	check(escaped.startsWith(QLatin1String("<pre")), QStringLiteral("返回值自带 <pre> 包裹"));

	// 主题包缺键时必须退到内置兜底表：这条断言防的是"高亮整片消失"（`<exe>/ui/` 旧主题包踩过）
	hl.setThemeColors(QHash<QString, QString>(), true);
	const QString plain = hl.highlight(QStringLiteral("python"), pyCode);
	check(hasSpan(plain) && plain.contains(QLatin1String("#FAA2C1")),
	      QStringLiteral("主题包缺键时退到内置兜底色（暗色 keyword #FAA2C1），高亮不消失"));
	hl.setThemeColors(dark, true);   // 还原，避免影响后面的断言

	// 规则文件体检：每条 pattern 都要能被 QRegularExpression 编译（否则该条永远不命中）
	QFile f(rulesPath);
	if (f.open(QIODevice::ReadOnly)) {
		const QJsonObject langs = QJsonDocument::fromJson(f.readAll()).object()
			.value(QStringLiteral("languages")).toObject();
		int total = 0;
		int invalid = 0;
		int noRole = 0;
		QString firstInvalid;
		for (auto it = langs.constBegin(); it != langs.constEnd(); ++it) {
			for (const QJsonValue& v : it.value().toArray()) {
				const QJsonObject rule = v.toObject();
				const QRegularExpression re(rule.value(QStringLiteral("pattern")).toString());
				++total;
				if (!re.isValid()) {
					++invalid;
					if (firstInvalid.isEmpty())
						firstInvalid = it.key() + QStringLiteral(": ") + rule.value(QStringLiteral("pattern")).toString();
				}
				// 每条规则必须写角色（或显式 color 覆盖），否则渲染时不着色
				if (rule.value(QStringLiteral("role")).toString().isEmpty()
					&& rule.value(QStringLiteral("color")).toString().isEmpty())
					++noRole;
			}
		}
		std::printf("    [rules] 语言 %lld 个、规则 %d 条、PCRE2 编译失败 %d 条、缺角色 %d 条\n",
		            static_cast<long long>(langs.size()), total, invalid, noRole);
		check(total > 300, QStringLiteral("规则数量"), QString::number(total));
		check(invalid == 0, QStringLiteral("全部规则都能被 PCRE2 编译"), firstInvalid);
		check(noRole == 0, QStringLiteral("每条规则都写了角色"));
	}
	else {
		check(false, QStringLiteral("规则文件可读"), rulesPath);
	}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	// 仓库根：selfcheck.exe 在 <repo>/build/cpp-selfcheck/，规则文件在 <repo>/assets/...
	const QString repoRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/../../");
	urlCases();
	modelCases();
	highlightCases(repoRoot);
	std::printf("\n=== 合计：%s（失败 %d 项）===\n", g_fail == 0 ? "全部 PASS" : "有 FAIL", g_fail);
	return g_fail == 0 ? 0 : 1;
}
