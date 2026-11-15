#pragma once

#include "common/agent/ulincore/UlinCoreTypes.h"

#include <QJsonArray>
#include <QObject>
#include <functional>

namespace UlinCore
{
struct ModelRequest
{
	QString systemPrompt;
	QList<Message> messages;
	QJsonArray tools;          // OpenAI function schema 数组
	double temperature = 0.2;
	int maxTokens = 4096;
};

struct ModelCallbacks
{
	std::function<void(const QString& textDelta, const QString& thinkingDelta)> onDelta;
	std::function<void(const QList<ToolCall>& calls, const QString& text, const QString& thinking,
		const Usage& usage, const QString& finishReason)> onComplete;
	std::function<void(const QString& error)> onError;
};

// 模型适配层：内核只要求「流式增量 + 最终工具调用 + 用量」这三件事。
// 具体实现可以是 HTTP（OpenAI 兼容）、DSH 的 session/prompt，或者测试用的 Echo。
class UlinCoreModel : public QObject
{
	Q_OBJECT

public:
	explicit UlinCoreModel(QObject* parent = nullptr) : QObject(parent) {}
	~UlinCoreModel() override = default;

	virtual QString name() const = 0;
	virtual void start(const ModelRequest& request, const ModelCallbacks& callbacks) = 0;
	virtual void cancel() = 0;
};
} // namespace UlinCore
