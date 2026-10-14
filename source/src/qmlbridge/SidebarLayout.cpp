// 见 SidebarLayout.h：这里只转发，算法在 common/layout/SidebarMetrics（每个方法一行）。
// 分层规矩：qmlbridge 只做接线（Q_INVOKABLE 转发），不放算法。

#include "qmlbridge/SidebarLayout.h"

#include "common/layout/SidebarMetrics.h"

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
	return SidebarMetrics::conversationMinWidth(shellWidth, tokenMinConversationWidth);
}

double SidebarLayout::maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
	double windowMargin, double sidebarWidth, double panelGap) const
{
	return SidebarMetrics::maxWidth(shellWidth, minWidth, conversationMinWidth, windowMargin,
		sidebarWidth, panelGap);
}

double SidebarLayout::clampWidth(double width, double minWidth, double maxWidth) const
{
	return SidebarMetrics::clampWidth(width, minWidth, maxWidth);
}

double SidebarLayout::widthFromDrag(double pressX, double pressWidth, double cursorX,
	double minWidth, double maxWidth) const
{
	return SidebarMetrics::widthFromDrag(pressX, pressWidth, cursorX, minWidth, maxWidth);
}
