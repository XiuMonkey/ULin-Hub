#include "qmlbridge/BridgeUlinCoreSettings.h"

UlinCoreSettingsBridge::UlinCoreSettingsBridge(QObject* parent)
	: QObject(parent)
{
	connect(&UlinCore::UlinCoreSettings::instance(), &UlinCore::UlinCoreSettings::changed,
		this, &UlinCoreSettingsBridge::changed);
	connect(&UlinCore::UlinCoreSettings::instance(), &UlinCore::UlinCoreSettings::statusChanged,
		this, &UlinCoreSettingsBridge::statusChanged);
}