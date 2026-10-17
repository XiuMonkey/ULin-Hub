// 见 PanelBarLayout.h：这里只转发，算法在 common/layout/PanelBarMetrics（每个方法一行）。
// 分层规矩：qmlbridge 只做接线（Q_INVOKABLE 转发），不放算法。

#include "qmlbridge/PanelBarLayout.h"

#include "common/layout/PanelBarMetrics.h"

PanelBarLayout& PanelBarLayout::instance()
{
	static PanelBarLayout layout;
	return layout;
}

PanelBarLayout::PanelBarLayout(QObject* parent)
	: QObject(parent)
{
}

double PanelBarLayout::conversationMinWidth(double shellWidth, double tokenMinConversationWidth) const
{
	return PanelBarMetrics::conversationMinWidth(shellWidth, tokenMinConversationWidth);
}

double PanelBarLayout::maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
	double windowMargin, double sessionBarWidth, double panelGap) const
{
	return PanelBarMetrics::maxWidth(shellWidth, minWidth, conversationMinWidth, windowMargin,
		sessionBarWidth, panelGap);
}

double PanelBarLayout::clampWidth(double width, double minWidth, double maxWidth) const
{
	return PanelBarMetrics::clampWidth(width, minWidth, maxWidth);
}

double PanelBarLayout::widthFromDrag(double pressX, double pressWidth, double cursorX,
	double minWidth, double maxWidth) const
{
	return PanelBarMetrics::widthFromDrag(pressX, pressWidth, cursorX, minWidth, maxWidth);
}
