// 见 PanelBarMetrics.h：右侧栏宽度的纯数值策略（自由函数、无 QObject、无 QML 依赖）。
// 公式与搬到 common 之前逐字等价；qmlbridge/PanelBarLayout 只是转发。

#include "common/layout/PanelBarMetrics.h"

#include <algorithm>

namespace
{
	// 会话区保底宽度的窗口比例（原 Shell.qml 的 `width * 0.25`，是策略所以放 C++）
	const double kConversationMinRatio = 0.25;
}

namespace PanelBarMetrics
{
	double conversationMinWidth(double shellWidth, double tokenMinConversationWidth)
	{
		return std::min(tokenMinConversationWidth, shellWidth * kConversationMinRatio);
	}

	double maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
		double windowMargin, double sessionBarWidth, double panelGap)
	{
		const double available = shellWidth - 2.0 * windowMargin - sessionBarWidth - panelGap - conversationMinWidth;
		return std::max(minWidth, available);
	}

	double clampWidth(double width, double minWidth, double maxWidth)
	{
		return std::max(minWidth, std::min(width, maxWidth));
	}

	double widthFromDrag(double pressX, double pressWidth, double cursorX,
		double minWidth, double maxWidth)
	{
		return clampWidth(pressWidth - (cursorX - pressX), minWidth, maxWidth);
	}
}
