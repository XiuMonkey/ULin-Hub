#pragma once

// ThemeController —— 主题切换的编排（拆自 qmlbridge/ShellController，2026-10-10）。
//
// 为什么单独一个类：切主题**不是接线**，它是一条跨越四个组件的流程 ——
//   读当前模式 → 写 AppearanceSetting（用户显式选择，落盘）→ ThemeManager::setMode（切模式 + 重建调色板缓存）
//   → ThemeBridge::reload（QML 侧 token 热重载）→ WindowBridge::notifyThemeChanged（原生窗口跟着换）
//   → 一张过渡卡片显示 ~120ms 再收掉（原版 #themeSwitchBody 的等价物）。
// 其中"过渡卡片时长"是实现细节（不是业务），注释里写清它只是让用户看到一瞬。
//
// 放在 qmlbridge/ 而不是 core/：它要驱动 QML 侧的两个单例（ThemeBridge / WindowBridge）。
// 若将来把"主题"整体收进 common/appearance，这个类应该跟着搬过去并去掉对 QML 单例的直接依赖
// （改成发信号，由 qmlbridge 转发）。

#include <QObject>
#include <QTimer>

class ThemeController : public QObject
{
	Q_OBJECT

public:
	explicit ThemeController(QObject* parent = nullptr);

	// 切换浅/深（用户在侧栏点主题键，或设置页里选）：写盘 + 切模式 + 让 QML token 热重载
	void toggle();
	// 过渡卡片是否正显示（QML 绑定它来画主题切换遮罩）
	bool switching() const { return m_switching; }

signals:
	void switchingChanged();

private:
	QTimer m_switchingTimer;   // 单次：把"过渡卡片"收掉
	bool m_switching = false;
};
