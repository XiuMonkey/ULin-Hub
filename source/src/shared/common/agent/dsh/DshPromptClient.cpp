#include "common/agent/dsh/DshPromptClient.h"

#include "common/session/ChatController.h"
#include "common/agent/dsh/CommandController.h"
#include "common/util/ConnectionManager.h"

DshPromptClient::DshPromptClient(CommandController* commands, ChatController* chat, QObject* parent)
	: QObject(parent)
	, m_commands(commands)
	, m_chat(chat)
{
	// 命令执行结果回发给 DSH，失败时由 onCommandFinished 追加系统提示。
	if (m_commands) {
		dshRegister("agent.dsh.command.result", m_commands, &CommandController::executionFinished, this, &DshPromptClient::onCommandFinished);
	}
}

void DshPromptClient::handlePrompt(const QString& sessionId, const QString& text)
{
	// 形如命令的输入交给命令通道，其余按原始 prompt 直接发送。
	if (m_commands && CommandController::looksLikeCommand(text)) {
		m_commands->execute(sessionId, text);
		return;
	}
	if (m_chat)
		m_chat->sendRawPrompt(sessionId, text);
}

void DshPromptClient::onCommandFinished(bool ok, const QString& message)
{
	if (ok || !m_chat)
		return;
	m_chat->appendSystemNotice(message, true);
}