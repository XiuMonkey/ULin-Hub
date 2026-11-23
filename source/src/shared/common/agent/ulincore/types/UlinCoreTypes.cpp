#include "common/agent/ulincore/types/UlinCoreTypes.h"

#include <QDateTime>
#include <QJsonArray>

namespace UlinCore
{
// 落 JSON 时采用 OpenAI 兼容的角色名，工具结果记为 tool。
QString roleName(Role role)
{
	switch (role) {
	case Role::System:
		return QStringLiteral("system");
	case Role::User:
		return QStringLiteral("user");
	case Role::Assistant:
		return QStringLiteral("assistant");
	case Role::IntentResponse:
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
		return Role::IntentResponse;
	return Role::User;
}

QJsonObject IntentCall::toJson() const
{
	return QJsonObject{
		{ QStringLiteral("id"), id },
		{ QStringLiteral("name"), name },
		{ QStringLiteral("arguments"), arguments },
		{ QStringLiteral("metadata"), metadata }
	};
}

IntentCall IntentCall::fromJson(const QJsonObject& json)
{
	IntentCall call;
	call.id = json.value(QStringLiteral("id")).toString();
	call.name = json.value(QStringLiteral("name")).toString();
	call.arguments = json.value(QStringLiteral("arguments")).toObject();
	call.metadata = json.value(QStringLiteral("metadata")).toObject();
	return call;
}

QJsonObject Message::toJson() const
{
	QJsonArray calls;
	for (const IntentCall& call : intentCalls)
		calls.append(call.toJson());

	return QJsonObject{
		{ QStringLiteral("role"), roleName(role) },
		{ QStringLiteral("text"), text },
		{ QStringLiteral("thinking"), thinking },
		{ QStringLiteral("intentCalls"), calls },
		{ QStringLiteral("intentCallId"), intentCallId },
		{ QStringLiteral("intentName"), intentName },
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
	// 优先读 intentCalls，旧数据回退到 toolCalls。
	const QJsonArray calls = json.value(QStringLiteral("intentCalls")).toArray().isEmpty()
		? json.value(QStringLiteral("toolCalls")).toArray()
		: json.value(QStringLiteral("intentCalls")).toArray();
	for (const QJsonValue& value : calls)
		message.intentCalls.append(IntentCall::fromJson(value.toObject()));
	// 兼容旧字段名 toolCallId / toolName，读不到新字段时回退。
	message.intentCallId = json.value(QStringLiteral("intentCallId")).toString(json.value(QStringLiteral("toolCallId")).toString());
	message.intentName = json.value(QStringLiteral("intentName")).toString(json.value(QStringLiteral("toolName")).toString());
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

Message Message::intentResult(const QString& callId, const QString& name, const QString& text, bool isError)
{
	Message message;
	message.role = Role::IntentResponse;
	message.intentCallId = callId;
	message.intentName = name;
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

ExecuteResult ExecuteResult::success(const QString& content, const QJsonObject& data)
{
	ExecuteResult result;
	result.ok = true;
	result.content = content;
	result.data = data;
	return result;
}

// 失败结果同时保留 error 与 "error: ..." 文本，便于直接回灌模型。
ExecuteResult ExecuteResult::failure(const QString& error)
{
	ExecuteResult result;
	result.ok = false;
	result.error = error;
	result.content = QStringLiteral("error: ") + error;
	return result;
}

IntentResult IntentResult::success(const QString& content, const QJsonObject& data)
{
	IntentResult result;
	result.ok = true;
	result.content = content;
	result.data = data;
	return result;
}

IntentResult IntentResult::failure(const QString& error)
{
	IntentResult result;
	result.ok = false;
	result.error = error;
	result.content = QStringLiteral("error: ") + error;
	return result;
}
} // namespace UlinCore
