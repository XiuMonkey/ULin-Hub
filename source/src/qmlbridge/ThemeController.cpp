// 见 ThemeController.h：切主题的编排。

#include "qmlbridge/ThemeController.h"

#include "common/appearance/ThemeManager.h"
#include "common/settings/ClientSettings.h"
#include "qmlbridge/ThemeBridge.h"
#include "qmlbridge/WindowBridge.h"

namespace
{
	constexpr int kTransitionMs = 120;   // 过渡卡片的显示时长：原版是"强推一帧后继续"，QML 里让它占一拍，用户能看到一瞬
}

ThemeController::ThemeController(QObject* parent)
	: QObject(parent)
{
	m_switchingTimer.setSingleShot(true);
	m_switchingTimer.setInterval(kTransitionMs);
	connect(&m_switchingTimer, &QTimer::timeout, this, [this]() {
		m_switching = false;
		emit switchingChanged();
	});
}

void ThemeController::toggle()
{
	// 原版路径：Sidebar::themeToggleRequested -> ULinHub::toggleTheme -> ThemeManager::switchTheme
	// switchTheme = 过渡卡片 + 写文件 + 切模式 + **重建主窗口**（复用旧的服务端进程）。
	// QML 壳不重建窗口：token 是绑定读取的，切模式后 reload 一次就让整棵树换肤。
	using Mode = ThemeManager::Mode;
	const Mode next = ThemeManager::instance().isDark() ? Mode::Light : Mode::Dark;

	AppearanceSetting::setThemeMode(next == Mode::Dark   // 先写文件再切模式
		? AppearanceSetting::ThemeMode::Dark
		: AppearanceSetting::ThemeMode::Light);

	m_switching = true;
	emit switchingChanged();

	ThemeManager::instance().setMode(next);
	ThemeBridge::instance().reload();
	WindowBridge::instance().notifyThemeChanged();

	m_switchingTimer.start();
}
