// 搬家后的真 C++ 自检（不依赖 QML/引擎）：直接链接被测的 4 个 .cpp。
// 覆盖：① UrlInput 规范化 ② PanelDockModel 的 open/close/activate/showHome ③ SidebarLayout 钳制与拖动换算。
// 两个被测类都是"程序级唯一实例、刻意不析构"的单例（QML 单例注册要求活过引擎）⇒ 用 instance()。

#include "common/util/UrlInput.h"
#include "qmlbridge/PanelDockModel.h"
#include "qmlbridge/SidebarLayout.h"

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
	std::printf("\n=== ② PanelDockModel ===\n");
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
}

static void sidebarCases()
{
	std::printf("\n=== ③ SidebarLayout ===\n");
	SidebarLayout& layout = SidebarLayout::instance();
	const double convMin = layout.conversationMinWidth(1232.0, 308.0);
	const double maxW = layout.maxWidth(1232.0, 260.0, convMin, 12.0, 311.0, 12.0);
	const double dragLeft = layout.widthFromDrag(100.0, 300.0, 50.0, 260.0, maxW);
	const double dragRight = layout.widthFromDrag(100.0, 300.0, 500.0, 260.0, maxW);
	std::printf("    convMin(1232,308)=%.1f  maxW=%.1f  dragL(50)=%.1f  dragR(500)=%.1f\n", convMin, maxW,
	            dragLeft, dragRight);
	check(qFuzzyCompare(convMin, 308.0), QStringLiteral("对话区保底 = min(token, 窗口*0.25)"), QString::number(convMin));
	check(maxW >= 260.0, QStringLiteral("上限不低于最小宽度"), QString::number(maxW));
	check(qFuzzyCompare(dragLeft, 350.0), QStringLiteral("往左拖 50 ⇒ 300->350（1:1）"), QString::number(dragLeft));
	check(qFuzzyCompare(dragRight, 260.0), QStringLiteral("往右拖过头 ⇒ 夹在下限 260"), QString::number(dragRight));
	check(qFuzzyCompare(layout.clampWidth(10.0, 260.0, maxW), 260.0), QStringLiteral("低于下限夹到 min"));
	check(qFuzzyCompare(layout.clampWidth(9999.0, 260.0, maxW), maxW), QStringLiteral("超过上限夹到 max"));
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	urlCases();
	modelCases();
	sidebarCases();
	std::printf("\n=== 合计：%s（失败 %d 项）===\n", g_fail == 0 ? "全部 PASS" : "有 FAIL", g_fail);
	return g_fail == 0 ? 0 : 1;
}
