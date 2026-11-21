#include "common/agent/ulincore/types/UlinCoreTypes.h"

#include <QDateTime>
#include <QJsonArray>

namespace UlinCore
{
QString roleName(Role role)
{
	switch (role) {
	case Role::System:
		return QStringLiteral("system");
	case Role::User:
		return QStringLiteral("user");
	case Role::Assistant:
		return QStringLiteral("assistant");
	case Role::Tool:
		return QStringLiteral("tool");
	}
	return QStringLiteral("user");
}

Role roleFromName(const QString& name)
{
	if (name == QStringLiteral("system"))
		return Role::System;
	if (name == QStringLiteral("assistant"))
		return Role::Assistant;
	if (name == QStringLiteral("tool"))
		return Role::Tool;
	return Role::User;
}

QJsonObject ToolCall::toJson() const
{
	return QJsonObject{
		{ QStringLiteral("id"), id },
		{ QStringLiteral("name"), name },
		{ QStringLiteral("arguments"), arguments }
	};
}

ToolCall ToolCall::fromJson(const QJsonObject& json)
{
	ToolCall call;
	call.id = json.value(QStringLiteral("id")).toString();
	call.name = json.value(QStringLiteral("name")).toString();
	call.arguments = json.value(QStringLiteral("arguments")).toObject();
	return call;
}

QJsonObject Message::toJson() const
{
	QJsonArray calls;
	for (const ToolCall& call : toolCalls)
		calls.append(call.toJson());

	return QJsonObject{
		{ QStringLiteral("role"), roleName(role) },
		{ QStringLiteral("text"), text },
		{ QStringLiteral("thinking"), thinking },
		{ QStringLiteral("toolCalls"), calls },
		{ QStringLiteral("toolCallId"), toolCallId },
		{ QStringLiteral("toolName"), toolName },
		{ QStringLiteral("isError"), isError },
		{ QStringLiteral("createdAt"), createdAt }
	};
}

Message Message::fromJson(const QJsonObject& json)
{
	Message message;
	message.role = roleFromName(json.value(QStringLiteral("role")).toString());
	message.text = json.value(QStringLiteral("text")).toString();
	message.thinking = json.value(QStringLiteral("thinking")).toString();
	const QJsonArray calls = json.value(QStringLiteral("toolCalls")).toArray();
	for (const QJsonValue& value : calls)
		message.toolCalls.append(ToolCall::fromJson(value.toObject()));
	message.toolCallId = json.value(QStringLiteral("toolCallId")).toString();
	message.toolName = json.value(QStringLiteral("toolName")).toString();
	message.isError = json.value(QStringLiteral("isError")).toBool();
	message.createdAt = static_cast<qint64>(json.value(QStringLiteral("createdAt")).toDouble());
	return message;
}

Message Message::user(const QString& text)
{
	Message message;
	message.role = Role::User;
	message.text = text;
	message.createdAt = QDateTime::currentMSecsSinceEpoch();
	return message;
}

Message Message::assistant(const QString& text, const QString& thinking)
{
	Message message;
	message.role = Role::Assistant;
	message.text = text;
	message.thinking = thinking;
	message.createdAt = QDateTime::currentMSecsSinceEpoch();
	return message;
}

Message Message::toolResult(const QString& callId, const QString& name, const QString& text, bool isError)
{
	Message message;
	message.role = Role::Tool;
	message.toolCallId = callId;
	message.toolName = name;
	message.text = text;
	message.isError = isError;
	message.createdAt = QDateTime::currentMSecsSinceEpoch();
	return message;
}

void Usage::add(const Usage& other)
{
	promptTokens += other.promptTokens;
	completionTokens += other.completionTokens;
}

QJsonObject Usage::toJson() const
{
	return QJsonObject{
		{ QStringLiteral("promptTokens"), promptTokens },
		{ QStringLiteral("completionTokens"), completionTokens },
		{ QStringLiteral("totalTokens"), totalTokens() }
	};
}

Usage Usage::fromJson(const QJsonObject& json)
{
	Usage usage;
	usage.promptTokens = json.value(QStringLiteral("promptTokens")).toInt();
	usage.completionTokens = json.value(QStringLiteral("completionTokens")).toInt();
	return usage;
}

ToolResult ToolResult::success(const QString& content, const QJsonObject& data)
{
	ToolResult result;
	result.ok = true;
	result.content = content;
	result.data = data;
	return result;
}

ToolResult ToolResult::failure(const QString& error)
{
	ToolResult result;
	result.ok = false;
	result.error = error;
	result.content = QStringLiteral("error: ") + error;
	return result;
}
} // namespace UlinCore
