#include "qmlbridge/ThemeController.h"

#include "common/appearance/ThemeManager.h"

ThemeController::ThemeController(QObject* parent)
	: QObject(parent)
{
	m_switchingConnection = connect(&ThemeManager::instance(),
		&ThemeManager::themeSwitchingChanged, this, &ThemeController::switchingChanged);
}

void ThemeController::toggle()
{
	ThemeManager::instance().toggleTheme();
}

bool ThemeController::switching() const
{
	return ThemeManager::instance().themeSwitching();
}
