#pragma once

// 主题宿主接口。由 ThemeManager 实现（登记名 kThemeManager）。

#include <qplugin.h>

class QWidget;

class VirtualTheme
{
public:
	virtual ~VirtualTheme() = default;

	// 当前是否为深色模式。
	virtual bool ExternalIsDarkMode() = 0;

	// 重新读取 <exe>/ui 下的 theme-<mode>.json + metrics.json，只重载调色板。
	virtual bool ExternalReloadStyles() = 0;

	// 切换浅色/深色，不重建窗口；允许传 nullptr。
	virtual void ExternalSwitchTheme(QWidget* currentWindow) = 0;

	// 历史兼容桩：QSS 已删除，仅保留签名。
	virtual void ExternalApplyToWindow(QWidget* window) = 0;
};
Q_DECLARE_INTERFACE(VirtualTheme, "com.ulinhub.VirtualTheme/1.0")
