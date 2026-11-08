#pragma once

// 注册式 Master Agent —— 扩展实现，宿主通过 AgentManager 管理。
//
// 扩展在 attachHost 时调用 VirtualApiHost::RegisterMasterAgent(owner, this)；
// 用户在设置页切换 Master 时，宿主调用：
//   DeactivateMasterAgent(old) → ActivateMasterAgent(new)
//
// 注意：这里的 Activate/Deactivate 是后端会话生命周期，不是扩展 DLL 的构造/析构。
// 扩展对象一直由宿主 QPluginLoader 持有，切换时不重新加载插件。

#include <qplugin.h>

class VirtualMasterAgent
{
public:
	virtual ~VirtualMasterAgent() = default;

	// 稳定 id（不能是 "dsh"）、展示名、可选说明。
	virtual const char* MasterAgentId() const = 0;
	virtual const char* MasterAgentName() const = 0;
	virtual const char* MasterAgentDescription() const { return ""; }

	// 启动/停止自己的后端会话和前端入口；宿主在切换事务里成对调用。
	virtual void ActivateMasterAgent() = 0;
	virtual void DeactivateMasterAgent() = 0;
};
Q_DECLARE_INTERFACE(VirtualMasterAgent, "com.ulinhub.VirtualMasterAgent/1.0")
