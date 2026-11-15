#pragma once

// 内置 DSH 的 MasterAgent 身份对象。
//
// 内置 DSH 的请求本来就走 DshApiClient 自己的 HTTP/mux，不经过 takeover sink；
// 但在 AgentManager 里，它和 codex / workbuddy / ulincore 一样必须是一个
// VirtualMasterAgent + VirtualApiSink 对象，这样注册、激活、查询、卸载都走同一套
// 接口，不需要在 AgentManager 或宿主里为 "dsh" 写特殊分支。
//
// 如果有人在 dsh 处于 active 时错误地打开 takeover，OnOutboundRequest 会明确失败，
// 而不是静默把请求交给一个不存在的扩展后端。

#include "VirtualClass/VirtualApiTakeover.h"
#include "VirtualClass/VirtualMasterAgent.h"

#include <QObject>

class DshMasterAgent : public QObject,
					   public VirtualMasterAgent,
					   public VirtualApiSink
{
	Q_OBJECT
	Q_INTERFACES(VirtualMasterAgent VirtualApiSink)

public:
	explicit DshMasterAgent(VirtualApiHost* host, QObject* parent = nullptr);

	const char* MasterAgentId() const override { return "dsh"; }
	const char* MasterAgentName() const override { return "DSH"; }
	const char* MasterAgentDescription() const override
	{
		return "内置 Agent 后端（HTTP/mux 由 DshApiClient 直接处理）";
	}

	// 内置传输不需要启动/停止扩展会话；切换只改变 AgentManager 的 active 身份。
	void ActivateMasterAgent() override;
	void DeactivateMasterAgent() override;

	// 正常情况下不会被调用；dsh 不是 takeover 后端。
	void OnOutboundRequest(const char* rpcId, const char* method, const char* argsJson) override;

private:
	VirtualApiHost* m_host = nullptr;
};
