#include "qmlbridge/BridgeCommand.h"

CommandBridge::CommandBridge(CommandController* controller, QObject* parent)
	: QObject(parent)
	, m_controller(controller)
{
	connect(m_controller, &CommandController::itemsChanged, this, &CommandBridge::itemsChanged);
	connect(m_controller, &CommandController::busyChanged, this, &CommandBridge::busyChanged);
}