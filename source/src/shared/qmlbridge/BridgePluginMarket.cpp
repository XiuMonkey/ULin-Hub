#include "qmlbridge/BridgePluginMarket.h"

PluginMarketBridge::PluginMarketBridge(PluginMarketController* controller, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
{
	connect(m_controller, &PluginMarketController::openChanged, this, &PluginMarketBridge::openChanged);
	connect(m_controller, &PluginMarketController::busyChanged, this, &PluginMarketBridge::busyChanged);
	connect(m_controller, &PluginMarketController::statusChanged, this, &PluginMarketBridge::statusChanged);
	connect(m_controller, &PluginMarketController::viewChanged, this, &PluginMarketBridge::viewChanged);
	connect(m_controller, &PluginMarketController::installChanged, this, &PluginMarketBridge::installChanged);
	connect(m_controller, &PluginMarketController::progressChanged, this, &PluginMarketBridge::progressChanged);
	connect(m_controller, &PluginMarketController::blockedBuildsChanged, this, &PluginMarketBridge::blockedBuildsChanged);
}