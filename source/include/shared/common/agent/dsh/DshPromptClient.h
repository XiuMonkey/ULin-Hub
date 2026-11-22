#pragma once

#include <QObject>
#include <QString>

class ChatController;
class CommandController;

// DSH 专属输入客户端：QObject + 槽，不继承虚基类。
class DshPromptClient : public QObject
{
	Q_OBJECT

public:
	explicit DshPromptClient(CommandController* commands, ChatController* chat,
		QObject* parent = nullptr);

public slots:
	void handlePrompt(const QString& sessionId, const QString& text);
	void onCommandFinished(bool ok, const QString& message);

private:
	CommandController* m_commands = nullptr;
	ChatController* m_chat = nullptr;
};