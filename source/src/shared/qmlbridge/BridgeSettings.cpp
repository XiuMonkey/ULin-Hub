#include "qmlbridge/BridgeSettings.h"

SettingsBridge::SettingsBridge(SettingsController* controller, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
	, m_ulincoreBridge(new UlinCoreSettingsBridge(this))
{
	connect(m_controller, &SettingsController::openChanged, this, &SettingsBridge::openChanged);
	connect(m_controller, &SettingsController::busyChanged, this, &SettingsBridge::busyChanged);
	connect(m_controller, &SettingsController::viewChanged, this, &SettingsBridge::viewChanged);
	connect(m_controller, &SettingsController::statusChanged, this, &SettingsBridge::statusChanged);
	connect(m_controller, &SettingsController::noticeChanged, this, &SettingsBridge::noticeChanged);
	connect(m_controller, &SettingsController::formFeedbackChanged, this, &SettingsBridge::formFeedbackChanged);
	connect(m_controller, &SettingsController::candidatesChanged, this, &SettingsBridge::candidatesChanged);
	connect(m_controller, &SettingsController::pageChanged, this, &SettingsBridge::pageChanged);
	connect(m_controller, &SettingsController::presetsChanged, this, &SettingsBridge::presetsChanged);
	connect(m_controller, &SettingsController::masterBackendChanged, this, &SettingsBridge::masterBackendChanged);
	connect(m_controller, &SettingsController::switchingBackendChanged, this, &SettingsBridge::switchingBackendChanged);
	connect(m_controller, &SettingsController::serverUrlChanged, this, &SettingsBridge::serverUrlChanged);
	connect(m_controller, &SettingsController::languageChanged, this, &SettingsBridge::languageChanged);
	connect(m_controller, &SettingsController::accountChanged, this, &SettingsBridge::accountChanged);
	connect(m_controller, &SettingsController::accountOpenChanged, this, &SettingsBridge::accountOpenChanged);
	connect(m_controller, &SettingsController::accountLoginHintChanged, this, &SettingsBridge::accountLoginHintChanged);
}