#pragma once

// 右侧栏（面板坞）宽度的**纯数值策略** —— 从 qmlbridge/SidebarLayout 里抽出来的算法本体。
//
// 分层规矩（本次领域归位，2026-10-15）：
//   · common/**    = 领域与纯逻辑（解析、策略、状态机、模型本体）。本文件属这一层：**自由函数、没有
//                    QObject**，不 import 任何 QML 概念，可以脱离 QML 引擎单测（见
//                    misc/tools/cpp-selfcheck/selfcheck.cpp 的 ③）。
//   · qmlbridge/** = 只做接线（属性/信号、Q_INVOKABLE 转发、QAbstractListModel 的角色名映射），不放算法。
//                    QML 单例 `SidebarLayout`（qmlbridge/SidebarLayout.h）现在只剩一行一行的转发。
//
// 为什么公式要单独一个文件：会话区保底的推导、上限的推导、夹取、拖动换算是**一串纯数值规则**
// （输入是几个数与 token，输出是一个宽度），与窗口、与 QML 场景都无关。留在 QML 里没法规格化验证
// （拖拽方向/夹取范围算错时只能在真机上"拖一下看看"），也会随风格包被拷一份出来
// （Shell.qml 是可被风格包覆盖的单元）。
//
// ⚠️ 这套拖动是**实测过 1:1 跟手**的（连同"拖动期间冻结拖柄位置"那套，见 Shell.qml 的拖柄注释）：
//    拖柄照旧喂**窗口坐标**，按下的宽度照旧取当时**生效宽度**（rightSidebarEffW），于是这里算出来的值
//    与搬家前逐位相同（都是 double 的四则运算）。本文件只做四则运算 + std::min/max，
//    **不要**夹带"取整/取整到像素"之类的处理：1:1 跟手依赖的就是"输入多少就算多少、不额外量化"。
//
// 带窗口尺寸的"策略"在这里，**具体 token 值仍在 QML/主题包里**（Theme.size/spacing）：这里只是公式，
// 不持有任何默认值，所以在风格包换掉 token 时不用改 C++。

namespace SidebarMetrics
{
	// 会话区保底宽度 = min(token 保底, 窗口宽 × 0.25)。
	// 为什么不能死守 token：窗口不大时，固定 240 侧栏 + 320 之后留给右侧栏的额度可能比默认宽度
	// 还小 ⇒ 面板一开局就顶在上限、只能往右（变窄）拖，往左没反应。
	double conversationMinWidth(double shellWidth, double tokenMinConversationWidth);

	// 侧栏宽度上限：窗口 − 两侧留白 − 左侧栏 − 间距 − 会话区保底；但不低于最小宽度
	// （窗口极小时上限会低于下限，夹取必须还能收敛到一个有效值）。
	double maxWidth(double shellWidth, double minWidth, double conversationMinWidth,
		double windowMargin, double sidebarWidth, double panelGap);

	// 把宽度夹进 [minWidth, maxWidth]（窗口变小、或被拖到极端都不会越界）
	double clampWidth(double width, double minWidth, double maxWidth);

	// 拖动换算：按下时的光标窗口 x / 按下时的宽度 + 当前光标窗口 x ⇒ 新宽度；再夹一次。
	// 往左拖 ⇒ cursorX < pressX ⇒ 宽度变大。
	double widthFromDrag(double pressX, double pressWidth, double cursorX,
		double minWidth, double maxWidth);
}
