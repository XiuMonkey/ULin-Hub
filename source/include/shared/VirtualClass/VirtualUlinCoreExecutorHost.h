#pragma once

// ULinCore 原生执行器注册接口。由 UlinCoreMasterAgent 实现，
// 扩展通过 DshHostIndex::kUlinCoreExecutorHost 取宿主对象后 qobject_cast 使用。

#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"

#include <qplugin.h>

class VirtualUlinCoreExecutorHost
{
public:
	virtual ~VirtualUlinCoreExecutorHost() = default;

	// id 必须是完整执行器 ID，例如 "my.ext.executor"。
	// 宿主接管 executor 所有权；卸载扩展前必须 UnregisterExecutor。
	virtual bool RegisterExecutor(const char* id, UlinCore::UlinCoreNativeExecutor* executor) = 0;
	virtual void UnregisterExecutor(const char* id) = 0;
	virtual bool HasExecutor(const char* id) const = 0;
};

Q_DECLARE_INTERFACE(VirtualUlinCoreExecutorHost, "com.ulinhub.VirtualUlinCoreExecutorHost/1.0")
