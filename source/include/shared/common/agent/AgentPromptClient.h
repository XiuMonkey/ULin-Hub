#pragma once

#include <QString>

// 前端输入客户端：只处理“进入 session/prompt 之前”的后端专属语义。
// true = 已接管；false = ChatController 继续按普通 Prompt 发送。
class AgentPromptClient
{
public:
	virtual ~AgentPromptClient() = default;
	virtual bool submit(const QString& sessionId, const QString& text) = 0;
};
