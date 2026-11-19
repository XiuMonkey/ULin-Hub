#include "common/agent/ulincore/UlinCoreKernel.h"

#include "common/agent/ulincore/UlinCoreBinding.h"
#include "common/agent/ulincore/UlinCoreIntentStore.h"
#include "common/agent/ulincore/UlinCoreJournal.h"
#include "common/agent/ulincore/UlinCoreParser.h"
#include "common/agent/ulincore/UlinCoreRecipeTool.h"

#include <QDebug>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include <functional>
#include <utility>

namespace
{
class ToolRunnable : public QRunnable
{
public:
	explicit ToolRunnable(std::function<void()> fn)
		: m_fn(std::move(fn))
	{
		setAutoDelete(true);
	}

	void run() override { m_fn(); }

private:
	std::function<void()> m_fn;
};
} // namespace

namespace UlinCore
{
UlinCoreKernel::UlinCoreKernel(QObject* parent)
	: QObject(parent)
	, m_session(new UlinCoreSession(QString(), this))
	, m_toolPool(new QThreadPool(this))
{
	m_tools.addBuiltins();
	m_toolPool->setMaxThreadCount(qBound(1, m_options.toolThreads, 16));
}

UlinCoreKernel::~UlinCoreKernel() = default;

void UlinCoreKernel::setModel(std::shared_ptr<UlinCoreModel> model)
{
	if (m_running)
		cancel();
	m_model = std::move(model);
}

void UlinCoreKernel::setOptions(const Options& options)
{
	m_options = options;
	if (m_toolPool)
		m_toolPool->setMaxThreadCount(qBound(1, options.toolThreads, 16));
}

void UlinCoreKernel::setJournalPath(const QString& filePath)
{
	if (!m_journal)
		m_journal = std::make_unique<UlinCoreJournal>(filePath);
	else
		m_journal->setFilePath(filePath);
}

void UlinCoreKernel::setIntentsPath(const QString& filePath)
{
	m_intentsPath = filePath;
	m_intents.setFilePath(filePath);
	QString error;
	if (!m_intents.load(&error))
		qWarning().noquote() << "[UlinCore] intent store load failed:" << error;
	for (const Intent& intent : m_intents.all()) {
		if (intent.hasChain())
			registerIntentTool(intent);
	}
}

void UlinCoreKernel::run(const QString& userText)
{
	const QString text = userText.trimmed();
	if (text.isEmpty()) {
		emit errorOccurred(QStringLiteral("empty prompt"));
		return;
	}
	if (m_running) {
		emit errorOccurred(QStringLiteral("kernel is already running"));
		return;
	}

	runParsed(UlinCoreParser::parse(userText));
}

void UlinCoreKernel::runParsed(const UlinCoreInvocation& invocation)
{
	if (invocation.kind == InvocationKind::Error) {
		emit errorOccurred(invocation.error.isEmpty()
			? QStringLiteral("invalid intent syntax")
			: invocation.error);
		return;
	}

	if (invocation.kind == InvocationKind::Prompt || invocation.kind == InvocationKind::Empty) {
		QString prompt = invocation.promptText;
		if (prompt.trimmed().isEmpty())
			prompt = invocation.rawText;
		runPromptText(invocation.rawText, prompt);
		return;
	}

	const QString name = invocation.intentName;
	const bool known = m_intents.contains(name);
	Intent intent = known ? m_intents.find(name) : Intent();
	if (known) {
		const QJsonObject params = effectiveParameters(intent, invocation.args);
		const bool usePrompt = invocation.hasAsPrompt
			? invocation.asPrompt
			: (intent.hasPrompt() && !intent.hasChain());
		QString prompt = invocation.hasPromptTemplate
			? invocation.promptTemplate
			: intent.promptTemplate;

		if (usePrompt) {
			if (prompt.trimmed().isEmpty()) {
				emit errorOccurred(QStringLiteral("intent %1 has no prompt template").arg(name));
				return;
			}
			runPromptText(invocation.rawText,
				expandPromptTemplate(prompt, params));
			return;
		}

		if (!intent.hasChain()) {
			finishEmptyTurn(invocation.rawText);
			return;
		}
		runRecipeIntent(intent, params, invocation.rawText);
		return;
	}

	// 未知 Intent，但带了 Prompt：创建可复用 PromptIntent。
	if (invocation.hasPromptTemplate && !invocation.promptTemplate.trimmed().isEmpty()) {
		Intent promptIntent;
		promptIntent.name = name;
		promptIntent.description = QStringLiteral("Saved prompt intent");
		promptIntent.promptTemplate = invocation.promptTemplate;
		promptIntent.parameters = inferParameters(invocation.args);
		const bool persist = invocation.hasSave ? invocation.save : true;
		if (persist)
			addIntent(promptIntent);

		if (invocation.hasAsPrompt && !invocation.asPrompt
			&& !promptIntent.hasChain()) {
			finishEmptyTurn(invocation.rawText);
			return;
		}
		runPromptText(invocation.rawText,
			expandPromptTemplate(promptIntent.promptTemplate, invocation.args));
		return;
	}

	// 未知 Intent，但名称是已有工具：按直接工具命令执行。
	if (m_tools.find(name)) {
		Intent direct;
		direct.name = name;
		direct.description = QStringLiteral("Direct tool invocation");
		direct.parameters = inferParameters(invocation.args);
		IntentStep step;
		step.tool = name;
		QJsonObject refs;
		for (auto it = invocation.args.constBegin(); it != invocation.args.constEnd(); ++it) {
			refs.insert(it.key(), QJsonObject{
				{ QStringLiteral("$param"), it.key() }
			});
		}
		step.arguments = refs;
		direct.steps.append(step);

		if (invocation.hasSave && invocation.save)
			saveLiteralIntent(direct);

		runRecipeIntent(direct, invocation.args, invocation.rawText);
		return;
	}

	// 不是已注册 Intent，也不是工具：按 Prompt fallback。
	QString prompt = invocation.promptText.trimmed();
	if (prompt.isEmpty())
		prompt = invocation.rawText;
	runPromptText(invocation.rawText, prompt);
}

void UlinCoreKernel::runPromptText(const QString& rawText, const QString& promptText)
{
	if (!m_model) {
		emit errorOccurred(QStringLiteral("no model set"));
		return;
	}

	m_session->appendUserText(promptText);
	m_running = true;
	m_cancelled = false;
	m_steps = 0;
	m_turnUsage = Usage();
	m_turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	++m_generation;

	journal(QStringLiteral("turn/start"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("text"), rawText },
		{ QStringLiteral("prompt"), promptText }
	});
	emit turnStarted(m_turnId);
	startStep(m_generation);
}

void UlinCoreKernel::runRecipeIntent(const Intent& intent, const QJsonObject& args, const QString& rawText)
{
	m_session->appendUserText(rawText);
	m_running = true;
	m_cancelled = false;
	m_steps = 0;
	m_turnUsage = Usage();
	m_turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	++m_generation;

	const QString callId = QStringLiteral("intent-") + m_turnId;
	journal(QStringLiteral("turn/start"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("text"), rawText },
		{ QStringLiteral("intent"), intent.name },
		{ QStringLiteral("arguments"), args }
	});
	emit turnStarted(m_turnId);
	emit toolStarted(m_turnId, callId, intent.name, args);
	journal(QStringLiteral("tool/call"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), callId },
		{ QStringLiteral("name"), intent.name },
		{ QStringLiteral("arguments"), args }
	});

	const int generation = m_generation;
	const ToolContext context = m_options.toolContext;
	QPointer<UlinCoreKernel> self(this);
	auto recipe = std::make_shared<UlinCoreRecipeTool>(intent, &m_tools);
	m_toolPool->start(new ToolRunnable(
		[self, generation, callId, intent, args, context, recipe]() {
			const ToolResult result = recipe->execute(args, context);
			if (!self)
				return;
			QMetaObject::invokeMethod(self.data(),
				[self, generation, callId, intent, args, result]() {
					if (UlinCoreKernel* kernel = self.data())
						kernel->finishRecipe(generation, callId, intent, args, result);
				},
				Qt::QueuedConnection);
		}));
}

void UlinCoreKernel::finishRecipe(int generation, const QString& callId, const Intent& intent,
	const QJsonObject& args, const ToolResult& result)
{
	Q_UNUSED(args)
	if (generation != m_generation || !m_running || m_cancelled)
		return;

	QString content = result.ok
		? result.content
		: (result.error.isEmpty() ? QStringLiteral("intent failed") : result.error);
	if (content.size() > m_options.maxToolResultChars) {
		const int dropped = content.size() - m_options.maxToolResultChars;
		content = content.left(m_options.maxToolResultChars)
			+ QStringLiteral("\n...[truncated %1 chars]").arg(dropped);
	}

	ToolResult stored = result;
	stored.content = content;
	m_session->append(Message::toolResult(callId, intent.name, content, !result.ok));
	journal(QStringLiteral("tool/result"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), callId },
		{ QStringLiteral("name"), intent.name },
		{ QStringLiteral("ok"), result.ok },
		{ QStringLiteral("content"), content }
	});
	emit toolFinished(m_turnId, callId, intent.name, stored);

	if (!result.ok) {
		finishTurn(generation, false, result.error, QString(), false);
		return;
	}

	if (!content.isEmpty() && content != QStringLiteral("ok")) {
		Message assistant = Message::assistant(content);
		m_session->append(assistant);
		emit assistantMessage(assistant);
		journal(QStringLiteral("assistant/message"), QJsonObject{
			{ QStringLiteral("turnId"), m_turnId },
			{ QStringLiteral("text"), content }
		});
	}
	finishTurn(generation, true, QString(), content, false);
}

void UlinCoreKernel::finishEmptyTurn(const QString& rawText)
{
	m_session->appendUserText(rawText);
	m_running = true;
	m_cancelled = false;
	m_steps = 0;
	m_turnUsage = Usage();
	m_turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	++m_generation;
	journal(QStringLiteral("turn/start"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("text"), rawText }
	});
	emit turnStarted(m_turnId);
	finishTurn(m_generation, true, QString(), QString(), false);
}

QJsonObject UlinCoreKernel::effectiveParameters(const Intent& intent, const QJsonObject& args) const
{
	QJsonObject params;
	for (const IntentParameter& parameter : intent.parameters) {
		if (!parameter.name.isEmpty() && !parameter.defaultValue.isUndefined())
			params.insert(parameter.name, parameter.defaultValue);
	}
	for (auto it = args.constBegin(); it != args.constEnd(); ++it)
		params.insert(it.key(), it.value());
	return params;
}

void UlinCoreKernel::savePromptIntent(const UlinCoreInvocation& invocation, const QString& prompt)
{
	if (invocation.intentName.trimmed().isEmpty() || prompt.trimmed().isEmpty())
		return;
	Intent intent;
	intent.name = invocation.intentName;
	intent.description = QStringLiteral("Saved prompt intent");
	intent.promptTemplate = prompt;
	intent.parameters = inferParameters(invocation.args);
	addIntent(intent);
}

void UlinCoreKernel::saveLiteralIntent(const Intent& intent)
{
	if (!intent.hasChain() || intent.name.trimmed().isEmpty())
		return;
	// 命令名可以和内置工具同名：命令路径优先走已保存 Intent；
	// 工具路径仍保留内置工具，避免一个用户 Recipe 覆盖 Agent 的底层能力。
	addIntent(intent);
}

void UlinCoreKernel::addIntent(const Intent& intent)
{
	if (intent.name.trimmed().isEmpty())
		return;
	m_intents.add(intent);
	if (!m_intentsPath.isEmpty()) {
		QString error;
		if (!m_intents.save(&error))
			qWarning().noquote() << "[UlinCore] intent store save failed:" << error;
	}
	if (intent.hasChain())
		registerIntentTool(intent);
}

void UlinCoreKernel::registerIntentTool(const Intent& intent)
{
	if (!intent.hasChain())
		return;
	// 同名内置工具优先保留给 Agent；命令入口通过 IntentStore 命中。
	if (m_tools.find(intent.name))
		return;
	m_tools.add(std::make_shared<UlinCoreRecipeTool>(intent, &m_tools));
}

void UlinCoreKernel::cancel()
{
	if (!m_running)
		return;

	// 先让旧代次的回调全部失效，再调模型的 cancel；否则 cancel 同步触发 onError
	// 会把这一轮写成失败而不是 cancelled。
	++m_generation;
	if (m_toolPool)
		m_toolPool->clear();   // 丢弃排队中的工具；正在跑的那个无法中断，回包会因代次失效被丢弃
	if (m_model)
		m_model->cancel();
	emitTurnFinished(false, QStringLiteral("cancelled"), QString(), true);
}

ModelCallbacks UlinCoreKernel::makeCallbacks(int generation)
{
	ModelCallbacks callbacks;
	callbacks.onDelta = [this, generation](const QString& text, const QString& thinking) {
		if (generation != m_generation || !m_running)
			return;
		m_pendingText += text;
		m_pendingThinking += thinking;
		emit assistantDelta(m_turnId, text, thinking);
	};
	callbacks.onComplete = [this, generation](const QList<ToolCall>& calls, const QString& text,
		const QString& thinking, const Usage& usage, const QString& finishReason) {
		handleComplete(generation, calls, text, thinking, usage, finishReason);
	};
	callbacks.onError = [this, generation](const QString& error) {
		if (generation != m_generation || !m_running)
			return;
		finishTurn(generation, false, error, QString(), false);
	};
	return callbacks;
}

void UlinCoreKernel::startStep(int generation)
{
	if (generation != m_generation || !m_running || m_cancelled)
		return;
	if (m_steps >= m_options.maxSteps) {
		finishTurn(generation, false,
			QStringLiteral("max steps exceeded (%1)").arg(m_options.maxSteps), QString(), false);
		return;
	}
	if (!m_model) {
		finishTurn(generation, false, QStringLiteral("no model set"), QString(), false);
		return;
	}

	m_pendingText.clear();
	m_pendingThinking.clear();

	ModelRequest request;
	request.systemPrompt = m_options.systemPrompt.isEmpty()
		? m_session->systemPrompt()
		: m_options.systemPrompt;
	request.messages = m_session->messages();
	request.tools = m_tools.schemas();

	const QString catalog = m_intents.catalogText();
	if (!catalog.isEmpty()) {
		request.systemPrompt += QStringLiteral(
			"\n\nSaved intents:\n%1\n"
			"When a saved intent declares parameters, refer to them with "
			"{\"$param\":\"name\"} in tool arguments instead of hard-coding the current value.")
			.arg(catalog);
	}

	const ModelCallbacks callbacks = makeCallbacks(generation);
	const std::shared_ptr<UlinCoreModel> model = m_model;
	QTimer::singleShot(0, this, [this, generation, request, callbacks, model] {
		if (generation != m_generation || !m_running || m_cancelled)
			return;
		model->start(request, callbacks);
	});
}

void UlinCoreKernel::handleComplete(int generation, const QList<ToolCall>& calls, const QString& text,
	const QString& thinking, const Usage& usage, const QString& finishReason)
{
	Q_UNUSED(finishReason)
	if (generation != m_generation || !m_running)
		return;

	++m_steps;
	m_turnUsage.add(usage);
	m_session->addUsage(usage);
	emit usageChanged(m_turnUsage);

	const QString fullText = text.isEmpty() ? m_pendingText : text;
	const QString fullThinking = thinking.isEmpty() ? m_pendingThinking : thinking;

	Message assistant = Message::assistant(fullText, fullThinking);
	assistant.toolCalls = calls;
	if (!fullText.isEmpty() || !calls.isEmpty()) {
		m_session->append(assistant);
		emit assistantMessage(assistant);
		journal(QStringLiteral("assistant/message"), QJsonObject{
			{ QStringLiteral("turnId"), m_turnId },
			{ QStringLiteral("text"), fullText },
			{ QStringLiteral("thinking"), fullThinking },
			{ QStringLiteral("toolCalls"), static_cast<int>(calls.size()) }
		});
	}

	if (calls.isEmpty()) {
		if (fullText.trimmed().isEmpty()) {
			finishTurn(generation, false,
				QStringLiteral("model returned an empty response"), QString(), false);
			return;
		}
		finishTurn(generation, true, QString(), fullText, false);
		return;
	}

	executeTool(generation, calls, 0);
}

void UlinCoreKernel::executeTool(int generation, const QList<ToolCall>& calls, int index)
{
	if (generation != m_generation || !m_running || m_cancelled)
		return;
	if (index >= calls.size()) {
		startStep(generation);
		return;
	}

	const ToolCall call = calls.at(index);
	emit toolStarted(m_turnId, call.id, call.name, call.arguments);
	journal(QStringLiteral("tool/call"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), call.id },
		{ QStringLiteral("name"), call.name },
		{ QStringLiteral("arguments"), call.arguments }
	});

	auto tool = m_tools.findShared(call.name);
	if (!tool) {
		completeTool(generation, calls, index, call,
			ToolResult::failure(QStringLiteral("unknown tool: %1").arg(call.name)));
		return;
	}

	const ToolContext context = m_options.toolContext;
	QPointer<UlinCoreKernel> self(this);
	m_toolPool->start(new ToolRunnable(
		[self, generation, calls, index, call, tool, context]() {
			const ToolResult result = tool->execute(call.arguments, context);
			if (!self)
				return;
			QMetaObject::invokeMethod(self.data(),
				[self, generation, calls, index, call, result]() {
					if (UlinCoreKernel* kernel = self.data())
						kernel->completeTool(generation, calls, index, call, result);
				},
				Qt::QueuedConnection);
		}));
}

void UlinCoreKernel::completeTool(int generation, const QList<ToolCall>& calls, int index,
	const ToolCall& call, const ToolResult& result)
{
	if (generation != m_generation || !m_running || m_cancelled)
		return;

	QString content = result.ok
		? result.content
		: (result.error.isEmpty() ? QStringLiteral("tool failed") : result.error);
	if (content.size() > m_options.maxToolResultChars) {
		const int dropped = content.size() - m_options.maxToolResultChars;
		content = content.left(m_options.maxToolResultChars)
			+ QStringLiteral("\n...[truncated %1 chars]").arg(dropped);
	}

	ToolResult stored = result;
	stored.content = content;

	m_session->append(Message::toolResult(call.id, call.name, content, !result.ok));
	journal(QStringLiteral("tool/result"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), call.id },
		{ QStringLiteral("name"), call.name },
		{ QStringLiteral("ok"), result.ok },
		{ QStringLiteral("content"), content }
	});
	emit toolFinished(m_turnId, call.id, call.name, stored);

	executeTool(generation, calls, index + 1);
}

void UlinCoreKernel::finishTurn(int generation, bool ok, const QString& error,
	const QString& finalText, bool cancelled)
{
	if (generation != m_generation)
		return;
	++m_generation;
	emitTurnFinished(ok, error, finalText, cancelled);
}

void UlinCoreKernel::emitTurnFinished(bool ok, const QString& error,
	const QString& finalText, bool cancelled)
{
	m_running = false;
	m_cancelled = cancelled;

	TurnResult result;
	result.ok = ok;
	result.cancelled = cancelled;
	result.error = error;
	result.finalText = finalText;
	result.steps = m_steps;
	result.usage = m_turnUsage;

	journal(QStringLiteral("turn/end"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("ok"), ok },
		{ QStringLiteral("cancelled"), cancelled },
		{ QStringLiteral("error"), error },
		{ QStringLiteral("steps"), m_steps }
	});
	if (!error.isEmpty() && !cancelled)
		emit errorOccurred(error);
	emit turnFinished(result);
}

void UlinCoreKernel::journal(const QString& type, const QJsonObject& payload)
{
	if (m_journal)
		m_journal->append(type, payload);
}
} // namespace UlinCore
