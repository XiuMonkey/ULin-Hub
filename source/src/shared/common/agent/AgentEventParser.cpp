// DSH Mux 流 JSON 解析接口实现；不依赖任何 UI 类，可单独复用。

#include "common/agent/AgentEventParser.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace
{
// ULinCore 落盘事件把 message 放在事件顶层；DSH 流式事件放在 data.message。
// 两种形状都要接受，否则历史快照会被解析成空消息。
QJsonObject messageObject(const QJsonObject& event)
{
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	QJsonObject message = data.value(QStringLiteral("message")).toObject();
	if (message.isEmpty())
		message = event.value(QStringLiteral("message")).toObject();
	if (message.isEmpty())
		message = data;
	return message;
}
} // namespace

QString extractEventText(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();

	if (type == QStringLiteral("assistant/message")
		|| type == QStringLiteral("user/message")) {
		const QJsonObject message = messageObject(event);
		const QJsonValue content = message.value(QStringLiteral("content"));

		if (content.isString())
			return content.toString();
		if (content.isArray()) {
			QStringList parts;
			const QJsonArray blocks = content.toArray();
			for (const auto& blockValue : blocks) {
				const QJsonObject block = blockValue.toObject();
				if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")
					|| block.contains(QStringLiteral("text"))) {
					parts << block.value(QStringLiteral("text")).toString();
				}
			}
			if (!parts.isEmpty())
				return parts.join(QLatin1Char('\n'));
		}
	}

	if (type == QStringLiteral("assistant/chunk")) {
		const QJsonValue chunk = data.value(QStringLiteral("chunk"));
		if (chunk.isString())
			return chunk.toString();
		if (chunk.isObject()) {
			const QJsonObject chunkObj = chunk.toObject();
			const QJsonValue text = chunkObj.value(QStringLiteral("text"));
			if (text.isString())
				return text.toString();
		}
	}

	const QJsonValue textValue = data.value(QStringLiteral("text"));  // 兜底：某些事件直接带 text
	if (textValue.isString())
		return textValue.toString();

	return QString();
}

QString extractChunkType(const QJsonObject& event)
{
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	const QJsonValue chunk = data.value(QStringLiteral("chunk"));

	if (chunk.isObject())
		return chunk.toObject().value(QStringLiteral("type")).toString();

	return QString();
}

QString extractThinking(const QJsonObject& event)
{
	const QJsonObject message = messageObject(event);
	const QJsonValue content = message.value(QStringLiteral("content"));

	if (!content.isArray())
		return QString();

	QStringList parts;
	const QJsonArray blocks = content.toArray();
	for (const auto& blockValue : blocks) {
		const QJsonObject block = blockValue.toObject();
		if (block.value(QStringLiteral("type")).toString() == QStringLiteral("reasoning")) {
			const QString text = block.value(QStringLiteral("text")).toString();
			if (!text.isEmpty())
				parts << text;
		}
	}

	return parts.join(QLatin1Char('\n'));
}

QString extractReply(const QJsonObject& event)
{
	const QJsonObject message = messageObject(event);
	const QJsonValue content = message.value(QStringLiteral("content"));

	if (content.isString())
		return content.toString();
	if (!content.isArray())
		return QString();

	QStringList parts;
	const QJsonArray blocks = content.toArray();
	for (const auto& blockValue : blocks) {
		const QJsonObject block = blockValue.toObject();
		if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
			const QString text = block.value(QStringLiteral("text")).toString();
			if (!text.isEmpty())
				parts << text;
		}
	}

	return parts.join(QLatin1Char('\n'));
}

IntentCallInfo extractIntentCall(const QJsonObject& event)
{
	IntentCallInfo info;
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	info.name = data.value(QStringLiteral("name")).toString();
	info.valid = !info.name.isEmpty();

	const QJsonValue argumentsValue = data.value(QStringLiteral("arguments"));
	if (argumentsValue.isObject()) {
		info.arguments = argumentsValue.toObject();
	}
	else if (argumentsValue.isString()) {
		info.arguments = QJsonDocument::fromJson(argumentsValue.toString().toUtf8()).object();
	}

	return info;
}

IntentResultInfo extractIntentResult(const QJsonObject& event)
{
	IntentResultInfo info;
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	info.message = data.value(QStringLiteral("message")).toString();
	info.valid = !info.message.isEmpty();
	return info;
}