#include "qmlbridge/BridgeSystem.h"

StyleBridge& StyleBridge::instance()
{
	static StyleBridge bridge;
	return bridge;
}

HostConnectionsBridge& HostConnectionsBridge::instance()
{
	static HostConnectionsBridge bridge;
	return bridge;
}

HostUiBridge& HostUiBridge::instance()
{
	static HostUiBridge bridge;
	return bridge;
}