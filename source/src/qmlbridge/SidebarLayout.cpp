// 见 SidebarLayout.h：右侧栏宽度的钳制策略。公式与搬家前的 Shell.qml 逐字等价。
//
// ⚠️ 这几个函数只做四则运算 + std::min/max，**不要**在这里夹带"取整/取整到像素"之类的处理：
//    1:1 跟手依赖的就是"输入多少就算多少、不额外量化"。

#include "qmlbridge/SidebarLayout.h"

#include <algorithm>

namespace
{
	// 会话区保底宽度的窗口比例（原 Shell.qml 的 `width * 0.25`，是策略所以放 C++）
	const double kConversationMinRatio = 0.25;
}

SidebarLayout& SidebarLayout::instance()
{
	static SidebarLayout layout;
	return layout;
}

SidebarLayout::SidebarLayout(QObject* parent)
	: QObject(parent)
{
}

double SidebarLayout::conversationMinWidth(double shellWidth, double tokenMinConversationWidth) const
{
	return std::min(tokenMinConversationWidth, shellWidth * kConversationMinRatio);
}

double SidebarLayout::maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
	double windowMargin, double sidebarWidth, double panelGap) const
{
	const double available = shellWidth - 2.0 * windowMargin - sidebarWidth - panelGap - conversationMinWidth;
	return std::max(minWidth, available);
}

double SidebarLayout::clampWidth(double width, double minWidth, double maxWidth) const
{
	return std::max(minWidth, std::min(width, maxWidth));
}

double SidebarLayout::widthFromDrag(double pressX, double pressWidth, double cursorX,
	double minWidth, double maxWidth) const
{
	return clampWidth(pressWidth - (cursorX - pressX), minWidth, maxWidth);
}
