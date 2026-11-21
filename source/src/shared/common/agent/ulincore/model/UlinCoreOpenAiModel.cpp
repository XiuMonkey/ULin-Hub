#include "common/agent/ulincore/model/UlinCoreOpenAiModel.h"

#include "common/agent/ulincore/model/protocol/UlinCoreModelProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace UlinCore
{
namespace
{
QString endpointFor(const QString& baseUrl)
{
	QString base = baseUrl.trimmed();
	while (base.endsWith(QLatin1Char('/')))
		base.chop(1);
	if (base.endsWith(QStringLiteral("/chat/completions")))
		return base;
	return base + QStringLiteral("/chat/completions");
}

QJsonObject messageToJson(const Message& message)
{
	QJsonObject object;
	switch (message.role) {
	case Role::System:
		object.insert(QStringLiteral("role"), QStringLiteral("system"));
		object.insert(QStringLiteral("content"), message.text);
		break;
	case Role::User:
		object.insert(QStringLiteral("role"), QStringLiteral("user"));
		object.insert(QStringLiteral("content"), message.text);
		break;
	case Role::Assistant: {
		object.insert(QStringLiteral("role"), QStringLiteral("assistant"));
		object.insert(QStringLiteral("content"), message.text);
		if (!message.intentCalls.isEmpty()) {
			QJsonArray calls;
			for (const IntentCall& call : message.intentCalls) {
				QJsonObject callObject{
					{ QStringLiteral("id"), call.id },
					{ QStringLiteral("type"), QStringLiteral("function") },
					{ QStringLiteral("function"), QJsonObject{
						{ QStringLiteral("name"), call.name },
						{ QStringLiteral("arguments"), QString::fromUtf8(
							QJsonDocument(Protocol::encodeToolArguments(call.arguments, call.metadata))
								.toJson(QJsonDocument::Compact)) }
					} }
				};
				calls.append(callObject);
			}
			object.insert(QStringLiteral("tool_calls"), calls);
		}
		break;
	}
	case Role::IntentResponse:
		object.insert(QStringLiteral("role"), QStringLiteral("tool"));
		object.insert(QStringLiteral("tool_call_id"), message.intentCallId);
		object.insert(QStringLiteral("content"), message.text);
		break;
	}
	return object;
}
} // namespace

UlinCoreOpenAiModel::UlinCoreOpenAiModel(OpenAiModelConfig config, QObject* parent)
	: UlinCoreModel(parent)
	, m_config(std::move(config))
	, m_network(new QNetworkAccessManager(this))
{
}

UlinCoreOpenAiModel::~UlinCoreOpenAiModel() = default;

void UlinCoreOpenAiModel::start(const ModelRequest& request, const ModelCallbacks& callbacks)
{
	cancel();   // 同一实例重复 start：先收掉上一条

	m_callbacks = callbacks;
	m_text.clear();
	m_thinking.clear();
	m_calls.clear();
	m_callArguments.clear();
	m_usage = Usage();
	m_finishReason.clear();
	m_buffer.clear();
	m_finished = false;
	m_cancelled = false;
	const int generation = ++m_generation;

	QJsonArray messages;
	if (!request.systemPrompt.isEmpty())
		messages.append(QJsonObject{
			{ QStringLiteral("role"), QStringLiteral("system") },
			{ QStringLiteral("content"), request.systemPrompt }
		});
	for (const Message& message : request.messages)
		messages.append(messageToJson(message));

	QJsonObject body{
		{ QStringLiteral("model"), m_config.model },
		{ QStringLiteral("messages"), messages },
		{ QStringLiteral("stream"), true },
		{ QStringLiteral("temperature"), m_config.temperature },
		{ QStringLiteral("max_tokens"), m_config.maxTokens }
	};
	if (!request.tools.isEmpty())
		body.insert(QStringLiteral("tools"), Protocol::encodeToolSchemas(request.tools));

	QNetworkRequest networkRequest(QUrl(endpointFor(m_config.baseUrl)));
	networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	networkRequest.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
	networkRequest.setTransferTimeout(180000);

	m_reply = m_network->post(networkRequest,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(m_reply, &QNetworkReply::readyRead, this, [this, generation] {
		if (generation == m_generation)
			handleReadyRead();
	});
	connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
		if (generation == m_generation)
			handleFinished();
	});
}

void UlinCoreOpenAiModel::cancel()
{
	m_cancelled = true;
	++m_generation;
	if (m_reply) {
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = nullptr;
	}
}

void UlinCoreOpenAiModel::handleReadyRead()
{
	if (!m_reply)
		return;

	m_buffer += m_reply->readAll();
	int newline = -1;
	while ((newline = m_buffer.indexOf('\n')) >= 0) {
		const QByteArray line = m_buffer.left(newline);
		m_buffer.remove(0, newline + 1);
		handleSseLine(line);
		if (m_finished)
			return;
	}
}

void UlinCoreOpenAiModel::handleSseLine(const QByteArray& line)
{
	const QByteArray trimmed = line.trimmed();
	if (!trimmed.startsWith("data:"))
		return;

	const QByteArray payload = trimmed.mid(5).trimmed();
	if (payload == "[DONE]") {
		complete();
		return;
	}

	const QJsonObject chunk = QJsonDocument::fromJson(payload).object();
	const QJsonArray choices = chunk.value(QStringLiteral("choices")).toArray();
	const QJsonObject choice = choices.isEmpty() ? QJsonObject() : choices.first().toObject();
	const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();

	const QString text = delta.value(QStringLiteral("content")).toString();
	QString thinking = delta.value(QStringLiteral("reasoning_content")).toString();
	if (thinking.isEmpty())
		thinking = delta.value(QStringLiteral("reasoning")).toString();

	if (!text.isEmpty() || !thinking.isEmpty()) {
		m_text += text;
		m_thinking += thinking;
		if (m_callbacks.onDelta)
			m_callbacks.onDelta(text, thinking);
	}

	const QJsonArray intentCalls = delta.value(QStringLiteral("tool_calls")).toArray();
	for (const QJsonValue& value : intentCalls) {
		const QJsonObject call = value.toObject();
		const int index = call.value(QStringLiteral("index")).toInt(m_calls.size());
		IntentCall& target = m_calls[index];
		if (call.contains(QStringLiteral("id")))
			target.id = call.value(QStringLiteral("id")).toString();
		if (call.value(QStringLiteral("metadata")).isObject())
			target.metadata = call.value(QStringLiteral("metadata")).toObject();

		const QJsonObject function = call.value(QStringLiteral("function")).toObject();
		if (function.contains(QStringLiteral("name")))
			target.name = function.value(QStringLiteral("name")).toString();
		if (function.value(QStringLiteral("metadata")).isObject())
			target.metadata = function.value(QStringLiteral("metadata")).toObject();
		const QJsonValue arguments = function.value(QStringLiteral("arguments"));
		if (arguments.isString())
			m_callArguments[index] += arguments.toString();
		else if (arguments.isObject())
			m_callArguments[index] += QString::fromUtf8(
				QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact));
	}

	const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
	if (!finishReason.isEmpty())
		m_finishReason = finishReason;

	const QJsonObject usage = chunk.value(QStringLiteral("usage")).toObject();
	if (!usage.isEmpty()) {
		m_usage.promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
		m_usage.completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
	}
}

void UlinCoreOpenAiModel::handleFinished()
{
	if (m_finished || m_cancelled || !m_reply)
		return;

	if (m_reply->error() != QNetworkReply::NoError) {
		const QByteArray body = m_reply->readAll();
		const QString detail = QString::fromUtf8(body.left(500)).trimmed();
		fail(m_reply->errorString() + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
		m_reply->deleteLater();
		m_reply = nullptr;
		return;
	}

	// 有些兼容实现不发 [DONE]：把残余缓冲吃完再收尾。
	m_buffer += m_reply->readAll();
	int newline = -1;
	while ((newline = m_buffer.indexOf('\n')) >= 0) {
		const QByteArray line = m_buffer.left(newline);
		m_buffer.remove(0, newline + 1);
		handleSseLine(line);
		if (m_finished)
			break;
	}
	complete();

	m_reply->deleteLater();
	m_reply = nullptr;
}

void UlinCoreOpenAiModel::complete()
{
	if (m_finished)
		return;
	m_finished = true;

	QList<IntentCall> calls;
	QList<int> keys = m_calls.keys();
	std::sort(keys.begin(), keys.end());
	for (int key : keys) {
		IntentCall call = m_calls.value(key);
		const QString arguments = m_callArguments.value(key);
		if (!arguments.isEmpty()) {
			QJsonParseError error{};
			const QJsonDocument document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
			if (document.isObject()) {
				QJsonObject metadata;
				call.arguments = Protocol::decodeToolArguments(document.object(), &metadata);
				if (!metadata.isEmpty())
					call.metadata = metadata;
			}
		}
		if (call.name.isEmpty())
			continue;
		if (call.id.isEmpty())
			call.id = QStringLiteral("call_%1").arg(key);
		calls.append(call);
	}

	if (m_callbacks.onComplete)
		m_callbacks.onComplete(calls, m_text, m_thinking, m_usage, m_finishReason);
	m_callbacks = ModelCallbacks();
}

void UlinCoreOpenAiModel::fail(const QString& error)
{
	if (m_finished)
		return;
	m_finished = true;
	if (m_callbacks.onError)
		m_callbacks.onError(error);
	m_callbacks = ModelCallbacks();
}
} // namespace UlinCore
