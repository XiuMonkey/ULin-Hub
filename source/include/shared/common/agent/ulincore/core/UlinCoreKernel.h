#pragma once

#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"
#include "common/agent/ulincore/intent/UlinCoreIntentCaller.h"
#include "common/agent/ulincore/intent/UlinCoreIntentStore.h"
#include "common/agent/ulincore/intent/UlinCoreInvocation.h"
#include "common/agent/ulincore/model/UlinCoreModel.h"
#include "common/agent/ulincore/core/UlinCoreSession.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <memory>

class QThreadPool;

namespace UlinCore
{
class UlinCoreJournal;

// 自研内核：user → 二次解析 → Intent / Prompt → model / tool → … → final。
// 只负责编排与状态机；模型、工具、持久化都可替换。
class UlinCoreKernel : public QObject
{
	Q_OBJECT

public:
	struct Options
	{
		int maxSteps = 24;
		int maxIntentResultChars = 20000;
		int toolThreads = 4;
		QString systemPrompt;
		ExecutorContext toolContext;
	};

	explicit UlinCoreKernel(QObject* parent = nullptr);
	~UlinCoreKernel() override;

	void setModel(std::shared_ptr<UlinCoreModel> model);
	std::shared_ptr<UlinCoreModel> model() const { return m_model; }

	UlinCoreSession* session() const { return m_session; }
	UlinCoreNativeRegistry& executors() { return m_executors; }
	UlinCoreIntentStore& intents() { return m_intents; }

	// data/ulincore/intents.json；加载后自动把有调用链的 Intent 注册成 Tool。
	void setIntentsPath(const QString& filePath);

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
	void toolFinished(const QString& turnId, const QString& callId, const QString& name, const UlinCore::IntentResult& result);
	void turnFinished(const UlinCore::TurnResult& result);
	void errorOccurred(const QString& message);
	void usageChanged(const UlinCore::Usage& usage);

private:
	ModelCallbacks makeCallbacks(int generation);
	void startStep(int generation);
	void handleComplete(int generation, const QList<IntentCall>& calls, const QString& text,
		const QString& thinking, const Usage& usage, const QString& finishReason);
	void callIntent(int generation, const QList<IntentCall>& calls, int index);
	void completeIntentCall(int generation, const QList<IntentCall>& calls, int index,
		const IntentCall& call, const IntentResult& result);
	void finishTurn(int generation, bool ok, const QString& error, const QString& finalText, bool cancelled);
	void emitTurnFinished(bool ok, const QString& error, const QString& finalText, bool cancelled);
	void journal(const QString& type, const QJsonObject& payload);

	// Intent 入口
	void runParsed(const UlinCoreInvocation& invocation);
	void runPromptText(const QString& rawText, const QString& promptText);
	void runRecipeIntent(const Intent& intent, const QJsonObject& args, const QString& rawText);
	void runNativeIntent(const Intent& intent, const QJsonObject& args, const QString& rawText);
	void finishRecipe(int generation, const QString& callId, const Intent& intent,
		const QJsonObject& args, const IntentResult& result);
	void finishEmptyTurn(const QString& rawText);
	void savePromptIntent(const UlinCoreInvocation& invocation, const QString& prompt);
	void saveLiteralIntent(const Intent& intent);
	void addIntent(const Intent& intent, bool persist = true);
	void installBuiltinIntents();
	QJsonArray executableToolSchemas() const;
	QJsonObject effectiveParameters(const Intent& intent, const QJsonObject& args) const;

	std::shared_ptr<UlinCoreModel> m_model;
	UlinCoreSession* m_session = nullptr;
	UlinCoreNativeRegistry m_executors;
	UlinCoreIntentStore m_intents;
	QString m_intentsPath;
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
