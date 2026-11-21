#pragma once

// 自研 Agent 内核的基础数据类型。
//
// 这里刻意不依赖 DSH 的 wire 格式：dsh/ 适配层负责把 DSH 的 session/event 翻译成
// Message / ToolCall / ToolResult，内核只认这些类型。这样换模型、换传输、做单测时，
// 内核本身不用改。

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace UlinCore
{
enum class Role
{
	System,
	User,
	Assistant,
	Tool
};

QString roleName(Role role);
Role roleFromName(const QString& name);

struct ToolCall
{
	QString id;
	QString name;
	QJsonObject arguments;
	QJsonObject metadata;   // tags / Intent；由 ULin 自有工具协议携带

	QJsonObject toJson() const;
	static ToolCall fromJson(const QJsonObject& json);
};

struct Message
{
	Role role = Role::User;
	QString text;
	QString thinking;
	QList<ToolCall> toolCalls;   // Assistant 消息里的工具调用
	QString toolCallId;          // Tool 消息对应哪次调用
	QString toolName;
	bool isError = false;
	qint64 createdAt = 0;

	QJsonObject toJson() const;
	static Message fromJson(const QJsonObject& json);

	static Message user(const QString& text);
	static Message assistant(const QString& text, const QString& thinking = QString());
	static Message toolResult(const QString& callId, const QString& name, const QString& text, bool isError);
};

struct Usage
{
	int promptTokens = 0;
	int completionTokens = 0;

	int totalTokens() const { return promptTokens + completionTokens; }
	void add(const Usage& other);
	QJsonObject toJson() const;
	static Usage fromJson(const QJsonObject& json);
};

struct ToolResult
{
	bool ok = false;
	QString content;
	QJsonObject data;
	QString error;

	static ToolResult success(const QString& content, const QJsonObject& data = {});
	static ToolResult failure(const QString& error);
};

struct TurnResult
{
	bool ok = false;
	bool cancelled = false;
	QString error;
	QString finalText;
	int steps = 0;
	Usage usage;
};
} // namespace UlinCore

Q_DECLARE_METATYPE(UlinCore::ToolCall)
Q_DECLARE_METATYPE(UlinCore::Message)
Q_DECLARE_METATYPE(UlinCore::Usage)
Q_DECLARE_METATYPE(UlinCore::ToolResult)
Q_DECLARE_METATYPE(UlinCore::TurnResult)
