#pragma once

// QML 单例 `PanelBarLayout`（注册见 src/core/main.cpp）—— **转发薄层**：
// 这里只转发，算法在 common/layout/PanelBarMetrics（自由函数、无 QObject）。
//
// 分层规矩（本次领域归位，2026-10-15）：
//   · common/**    = 领域与纯逻辑（解析、策略、状态机、模型本体）：右侧栏的宽度公式、1:1 跟手的约束、
//                    各条策略的"为什么"全在 common/layout/PanelBarMetrics.{h,cpp}，可脱离 QML 单测。
//   · qmlbridge/** = 只做接线（属性/信号、Q_INVOKABLE 转发、QAbstractListModel 的角色名映射），不放算法。
//                    本文件就是这条规矩的样板：四个方法各一行转发。
//
// 为什么本类还留着（而不是让 QML 直接 import 一堆自由函数）：QML 侧读的是
// `PanelBarLayout.conversationMinWidth(...)` / `PanelBarLayout.widthFromDrag(...)` 这类**单例方法调用**，
// 而单例名与方法签名是对外契约（main.cpp 的 qmlRegisterSingletonInstance("ULinHub", 1, 0, "PanelBarLayout", …)），
// 不能变。于是保留这个 QObject 单例当门面，算法一律下沉到 common/layout。

#include <QObject>

class PanelBarLayout : public QObject
{
	Q_OBJECT

public:
	static PanelBarLayout& instance();   // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎

	// ↓ 这四个方法都只转发，公式与注释见 common/layout/PanelBarMetrics.h（同名函数）
	Q_INVOKABLE double conversationMinWidth(double shellWidth, double tokenMinConversationWidth) const;
	Q_INVOKABLE double maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
		double windowMargin, double sessionBarWidth, double panelGap) const;
	Q_INVOKABLE double clampWidth(double width, double minWidth, double maxWidth) const;
	Q_INVOKABLE double widthFromDrag(double pressX, double pressWidth, double cursorX,
		double minWidth, double maxWidth) const;

private:
	explicit PanelBarLayout(QObject* parent = nullptr);
	PanelBarLayout(const PanelBarLayout&) = delete;
	PanelBarLayout& operator=(const PanelBarLayout&) = delete;
};
