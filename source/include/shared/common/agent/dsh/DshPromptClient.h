#pragma once

#include "common/agent/AgentPromptClient.h"

class CommandController;

// DSH 专属前端客户端：命令走 commands/execute，普通文本交给 ChatController。
class DshPromptClient : public AgentPromptClient
{
public:
	explicit DshPromptClient(CommandController* commands);

	bool submit(const QString& sessionId, const QString& text) override;

private:
	CommandController* m_commands = nullptr;
};
