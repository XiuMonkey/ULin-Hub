#include "common/agent/dsh/DshPromptClient.h"

#include "common/session/CommandController.h"

DshPromptClient::DshPromptClient(CommandController* commands)
	: m_commands(commands)
{
}

bool DshPromptClient::submit(const QString& sessionId, const QString& text)
{
	if (!m_commands || sessionId.isEmpty())
		return false;
	if (!CommandController::looksLikeCommand(text))
		return false;
	m_commands->execute(sessionId, text);
	return true;
}
