#pragma once

#include "common/agent/ulincore/UlinCoreModel.h"
#include "common/agent/ulincore/UlinCoreSession.h"
#include "common/agent/ulincore/UlinCoreTool.h"

#include <QJsonObject>
#include <QObject>
#include <memory>

class QThreadPool;

namespace UlinCore
{
class UlinCoreJournal;

// 自研内核：user → model → tool → model → … → final。
// 只负责编排与状态机；模型、工具、持久化都可替换。
class UlinCoreKernel : public QObject
{
	Q_OBJECT

public:
	struct Options
	{
		int maxSteps = 24;
		int maxToolResultChars = 20000;
		int toolThreads = 4;
		QString systemPrompt;
		ToolContext toolContext;
	};

	explicit UlinCoreKernel(QObject* parent = nullptr);
	~UlinCoreKernel() override;

	void setModel(std::shared_ptr<UlinCoreModel> model);
	std::shared_ptr<UlinCoreModel> model() const { return m_model; }

	UlinCoreSession* session() const { return m_session; }
	UlinCoreToolRegistry& tools() { return m_tools; }

	void setOptions(const Options& options);
	Options options() const { return m_options; }

	void setJournalPath(const QString& filePath);
	UlinCoreJournal* journal() const { return m_journal.get(); }

	bool running() const { return m_running; }

public slots:
	void run(const QString& userText);
	void cancel();

signals:
	void turnStarted(const QString& turnId);
	void assistantDelta(const QString& turnId, const QString& text, const QString& thinking);
	void assistantMessage(const UlinCore::Message& message);
	void toolStarted(const QString& turnId, const QString& callId, const QString& name, const QJsonObject& arguments);
	void toolFinished(const QString& turnId, const QString& callId, const QString& name, const UlinCore::ToolResult& result);
	void turnFinished(const UlinCore::TurnResult& result);
	void errorOccurred(const QString& message);
	void usageChanged(const UlinCore::Usage& usage);

private:
	ModelCallbacks makeCallbacks(int generation);
	void startStep(int generation);
	void handleComplete(int generation, const QList<ToolCall>& calls, const QString& text,
		const QString& thinking, const Usage& usage, const QString& finishReason);
	void executeTool(int generation, const QList<ToolCall>& calls, int index);
	void completeTool(int generation, const QList<ToolCall>& calls, int index,
		const ToolCall& call, const ToolResult& result);
	void finishTurn(int generation, bool ok, const QString& error, const QString& finalText, bool cancelled);
	void emitTurnFinished(bool ok, const QString& error, const QString& finalText, bool cancelled);
	void journal(const QString& type, const QJsonObject& payload);

	std::shared_ptr<UlinCoreModel> m_model;
	UlinCoreSession* m_session = nullptr;
	UlinCoreToolRegistry m_tools;
	Options m_options;
	std::unique_ptr<UlinCoreJournal> m_journal;
	QThreadPool* m_toolPool = nullptr;

	bool m_running = false;
	bool m_cancelled = false;
	int m_generation = 0;
	QString m_turnId;
	int m_steps = 0;
	QString m_pendingText;
	QString m_pendingThinking;
	Usage m_turnUsage;
};
} // namespace UlinCore
