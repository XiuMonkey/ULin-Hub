#pragma once

#include "common/agent/ulincore/model/UlinCoreModel.h"

#include <QHash>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace UlinCore
{
struct OpenAiModelConfig
{
	QString baseUrl;       // 例：https://api.deepseek.com/v1（也接受完整的 .../chat/completions）
	QString apiKey;
	QString model;         // 例：deepseek-chat
	double temperature = 0.2;
	int maxTokens = 4096;
};

// OpenAI 兼容的流式模型适配器：/chat/completions + SSE。
// 兼容 DeepSeek 的 reasoning_content，也兼容标准 OpenAI 的 tool_calls 增量。
class UlinCoreOpenAiModel : public UlinCoreModel
{
	Q_OBJECT

public:
	explicit UlinCoreOpenAiModel(OpenAiModelConfig config, QObject* parent = nullptr);
	~UlinCoreOpenAiModel() override;

	QString name() const override { return m_config.model; }
	void start(const ModelRequest& request, const ModelCallbacks& callbacks) override;
	void cancel() override;

private:
	void handleReadyRead();
	void handleFinished();
	void handleSseLine(const QByteArray& line);
	void fail(const QString& error);
	void complete();

	OpenAiModelConfig m_config;
	ModelCallbacks m_callbacks;
	QNetworkAccessManager* m_network = nullptr;
	QNetworkReply* m_reply = nullptr;
	QByteArray m_buffer;
	QString m_text;
	QString m_thinking;
	QHash<int, ToolCall> m_calls;
	QHash<int, QString> m_callArguments;
	Usage m_usage;
	QString m_finishReason;
	bool m_finished = false;
	bool m_cancelled = false;
	int m_generation = 0;
};
} // namespace UlinCore
