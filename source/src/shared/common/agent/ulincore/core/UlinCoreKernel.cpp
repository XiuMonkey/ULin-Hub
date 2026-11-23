#include "common/agent/ulincore/core/UlinCoreKernel.h"

#include "common/agent/ulincore/intent/UlinCoreBinding.h"
#include "common/agent/ulincore/intent/UlinCoreIntentStore.h"
#include "common/agent/ulincore/core/UlinCoreJournal.h"
#include "common/agent/ulincore/executor/UlinCoreBuiltinIntents.h"
#include "common/agent/ulincore/intent/UlinCoreParser.h"

#include <QDebug>
#include <QDateTime>
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
	m_executors.addBuiltins();
	installBuiltinIntents();
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
	installBuiltinIntents();
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
	IntentMode mode = invocation.mode;
	if (invocation.hasDefinition)
		mode = IntentMode::Definition;
	if (!invocation.execute && !invocation.hasDefinition) {
		emit errorOccurred(QStringLiteral("definition payload required for defineOnly"));
		return;
	}
	if (invocation.hasDefinition) {
		if (name.trimmed().isEmpty()) {
			emit errorOccurred(QStringLiteral("intent definition requires a name"));
			return;
		}
		Intent definition = invocation.definition;
		definition.name = name;
		if (!definition.hasPrompt() && !definition.hasSkill()
				&& !definition.hasChain() && !definition.hasNative()) {
			emit errorOccurred(QStringLiteral("intent %1 definition is empty").arg(name));
			return;
		}
		addIntent(definition, invocation.save());
		if (!invocation.execute)
			return;
		if (!definition.hasChain() && !definition.hasNative()) {
			emit errorOccurred(QStringLiteral("intent %1 has no executable form; use defineOnly").arg(name));
			return;
		}
	}

	const bool known = m_intents.contains(name);
	if (invocation.hasDefinition)
		mode = IntentMode::Definition;
	else if (known)
		mode = IntentMode::Execution;
	else
		mode = IntentMode::Generation;
	qInfo().noquote() << "[UlinCore] intent mode="
		<< (mode == IntentMode::Definition ? "definition"
			: mode == IntentMode::Generation ? "generation"
			: mode == IntentMode::Execution ? "execution" : "undetermined")
		<< "name=" << name;

	if (mode == IntentMode::Generation) {
		runGeneration(invocation);
		return;
	}

	if (mode == IntentMode::Execution && invocation.save()) {
		emit errorOccurred(QStringLiteral("intent %1 already exists; save requires definition/generation mode").arg(name));
		return;
	}

	Intent intent = known ? m_intents.find(name) : Intent();
	if (known) {
		const QJsonObject params = effectiveParameters(intent, invocation.args);
		if (invocation.asSkill()) {
			if (!invocation.hasPromptOverride) {
				emit errorOccurred(QStringLiteral("intent %1 asSkill requires an external prompt").arg(name));
				return;
			}
			const QString prompt = invocation.promptOverride;
			const QString method = invocation.hasSkillOverride
				? invocation.skillOverride : intent.skill;
			if (prompt.trimmed().isEmpty() || method.trimmed().isEmpty()) {
				emit errorOccurred(QStringLiteral("intent %1 needs external prompt and skill").arg(name));
				return;
			}
			runPromptText(invocation.rawText,
				expandPromptTemplate(prompt, params) + QStringLiteral("\n\n[Skill]\n")
					+ expandPromptTemplate(method, params));
			return;
		}

		if (invocation.withSkill()) {
			if (invocation.hasPromptOverride) {
				emit errorOccurred(QStringLiteral("intent %1 withSkill rejects external prompt").arg(name));
				return;
			}
			if (!invocation.asPrompt() && (intent.hasChain() || intent.hasNative())) {
				emit errorOccurred(QStringLiteral("intent %1 withSkill needs asPrompt for chain/native").arg(name));
				return;
			}
			const QString method = invocation.hasSkillOverride
				? invocation.skillOverride : intent.skill;
			if (intent.prompt.trimmed().isEmpty() || method.trimmed().isEmpty()) {
				emit errorOccurred(QStringLiteral("intent %1 needs built-in prompt and skill").arg(name));
				return;
			}
			runPromptText(invocation.rawText,
				expandPromptTemplate(intent.prompt, params) + QStringLiteral("\n\n[Skill]\n")
					+ expandPromptTemplate(method, params));
			return;
		}

		const bool usePrompt = invocation.asPrompt()
			? true
			: intent.defaultPromptMode();
		QString prompt = invocation.hasPromptOverride
			? invocation.promptOverride
			: intent.prompt;

		if (usePrompt) {
			if (prompt.trimmed().isEmpty()) {
				emit errorOccurred(QStringLiteral("intent %1 has no prompt template").arg(name));
				return;
			}
			runPromptText(invocation.rawText,
				expandPromptTemplate(prompt, params));
			return;
		}

		if (intent.hasNative()) {
			runNativeIntent(intent, params, invocation.rawText);
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
	if (invocation.hasPromptOverride && !invocation.promptOverride.trimmed().isEmpty()) {
		Intent promptIntent;
		promptIntent.name = name;
		promptIntent.description = QStringLiteral("Saved prompt intent");
		promptIntent.prompt = invocation.promptOverride;
		if (invocation.hasSkillOverride)
			promptIntent.skill = invocation.skillOverride;
		promptIntent.parameters = inferParameters(invocation.args);
		const bool persist = invocation.save();
		if (persist)
			addIntent(promptIntent);

		QString text = expandPromptTemplate(promptIntent.prompt, invocation.args);
		if (invocation.withSkill() && !promptIntent.skill.trimmed().isEmpty())
			text += QStringLiteral("\n\n[Skill]\n")
				+ expandPromptTemplate(promptIntent.skill, invocation.args);
		runPromptText(invocation.rawText, text);
		return;
	}

	// 不是已注册 Intent：按 Prompt fallback。
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
	const ExecutorContext context = m_options.toolContext;
	QPointer<UlinCoreKernel> self(this);
	auto resolver = [this](const QString& name) { return m_intents.find(name); };
	auto recipe = std::make_shared<UlinCoreIntentCaller>(intent, &m_executors, resolver);
	m_toolPool->start(new ToolRunnable(
		[self, generation, callId, intent, args, context, recipe]() {
			const IntentResult result = recipe->call(args, context);
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

void UlinCoreKernel::runNativeIntent(const Intent& intent, const QJsonObject& args, const QString& rawText)
{
	if (!intent.hasNative()) {
		finishEmptyTurn(rawText);
		return;
	}

	// Native Intent 直接交给 RecipeTool 的 native 分支：executor 是叶子，arguments 已实化。
	runRecipeIntent(intent, args, rawText);
}

void UlinCoreKernel::finishRecipe(int generation, const QString& callId, const Intent& intent,
	const QJsonObject& args, const IntentResult& result)
{
	Q_UNUSED(args)
	if (generation != m_generation || !m_running || m_cancelled)
		return;

	QString content = result.ok
		? result.content
		: (result.error.isEmpty() ? QStringLiteral("intent failed") : result.error);
	if (content.size() > m_options.maxIntentResultChars) {
		const int dropped = content.size() - m_options.maxIntentResultChars;
		content = content.left(m_options.maxIntentResultChars)
			+ QStringLiteral("\n...[truncated %1 chars]").arg(dropped);
	}

	IntentResult stored = result;
	stored.content = content;
	m_session->append(Message::intentResult(callId, intent.name, content, !result.ok));
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

void UlinCoreKernel::runGeneration(const UlinCoreInvocation& invocation)
{
	if (invocation.promptOverride.trimmed().isEmpty()) {
		emit errorOccurred(QStringLiteral("generation requires prompt"));
		return;
	}
	if (invocation.asPrompt() || invocation.asSkill() || invocation.withSkill()) {
		emit errorOccurred(QStringLiteral("generation mode cannot use execution tags"));
		return;
	}

	Intent generated;
	generated.name = invocation.intentName;
	generated.description = QStringLiteral("Generated intent");
	generated.prompt = invocation.promptOverride;
	generated.skill = invocation.hasSkillOverride ? invocation.skillOverride : QString();
	generated.parameters = inferParameters(invocation.args);

	if (invocation.skillRequired() && generated.skill.trimmed().isEmpty()) {
		runSkillAuthoring(invocation);
		return;
	}

	if (invocation.generateOnly() || invocation.agentEngaged()) {
		addIntent(generated, invocation.save());
		finishGeneratedTurn(invocation.rawText,
			QStringLiteral("intent %1 generated").arg(generated.name));
		return;
	}

	// 默认生成：先执行 seed，行为链在 finishTurn 时固化成 steps。
	m_generationPending = true;
	m_generationSave = invocation.save();
	m_generationName = generated.name;
	m_generationPrompt = generated.prompt;
	m_generationSkill = generated.skill;
	m_generationCalls.clear();
	runPromptText(invocation.rawText,
		expandPromptTemplate(generated.prompt, invocation.args));
}

void UlinCoreKernel::runSkillAuthoring(const UlinCoreInvocation& invocation)
{
	if (!m_model) {
		emit errorOccurred(QStringLiteral("skillRequired needs a model"));
		return;
	}

	m_session->appendUserText(invocation.rawText);
	m_running = true;
	m_cancelled = false;
	m_steps = 0;
	m_turnUsage = Usage();
	m_turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	++m_generation;
	const int generation = m_generation;
	journal(QStringLiteral("turn/start"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("text"), invocation.rawText }
	});
	emit turnStarted(m_turnId);

	ModelRequest request;
	request.systemPrompt = QStringLiteral(
		"You author reusable Intent skills. Return only the skill text, no markdown.");
	request.temperature = 0.2;
	request.maxTokens = 512;
	Message user;
	user.role = Role::User;
	user.text = QStringLiteral("Create a concise reusable skill for this goal:\n")
		+ invocation.promptOverride;
	request.messages.append(user);

	ModelCallbacks callbacks;
	callbacks.onDelta = [](const QString&, const QString&) {};
	callbacks.onComplete = [this, generation, invocation](const QList<IntentCall>&,
		const QString& text, const QString&, const Usage& usage, const QString&) {
		if (generation != m_generation || !m_running)
			return;
		m_turnUsage.add(usage);
		m_session->addUsage(usage);
		const QString skill = text.trimmed();
		if (skill.isEmpty()) {
			finishTurn(generation, false, QStringLiteral("skill generation returned empty"), QString(), false);
			return;
		}
		Intent generated;
		generated.name = invocation.intentName;
		generated.description = QStringLiteral("Generated intent");
		generated.prompt = invocation.promptOverride;
		generated.skill = skill;
		generated.parameters = inferParameters(invocation.args);
		addIntent(generated, invocation.save());

		Message assistant = Message::assistant(
			QStringLiteral("intent %1 generated").arg(generated.name));
		m_session->append(assistant);
		emit assistantMessage(assistant);
		finishTurn(generation, true, QString(), assistant.text, false);
	};
	callbacks.onError = [this, generation](const QString& error) {
		finishTurn(generation, false, error, QString(), false);
	};
	m_model->start(request, callbacks);
}

void UlinCoreKernel::finishGeneratedTurn(const QString& rawText, const QString& text)
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

	Message message;
	message.role = Role::Assistant;
	message.text = text;
	message.createdAt = QDateTime::currentMSecsSinceEpoch();
	m_session->append(message);
	emit assistantMessage(message);
	finishTurn(m_generation, true, QString(), text, false);
}

void UlinCoreKernel::savePromptIntent(const UlinCoreInvocation& invocation, const QString& prompt)
{
	if (invocation.intentName.trimmed().isEmpty() || prompt.trimmed().isEmpty())
		return;
	Intent intent;
	intent.name = invocation.intentName;
	intent.description = QStringLiteral("Saved prompt intent");
	intent.prompt = prompt;
	intent.parameters = inferParameters(invocation.args);
	addIntent(intent);
}

void UlinCoreKernel::saveLiteralIntent(const Intent& intent)
{
	if ((!intent.hasChain() && !intent.hasNative()) || intent.name.trimmed().isEmpty())
		return;
	// 命令名可以和内置工具同名：命令路径优先走已保存 Intent；
	// 工具路径仍保留内置工具，避免一个用户 Recipe 覆盖 Agent 的底层能力。
	addIntent(intent);
}

void UlinCoreKernel::addIntent(const Intent& intent, bool persist)
{
	if (intent.name.trimmed().isEmpty())
		return;
	m_intents.add(intent);
	if (persist && !m_intentsPath.isEmpty()) {
		QString error;
		if (!m_intents.save(&error))
			qWarning().noquote() << "[UlinCore] intent store save failed:" << error;
	}
}

void UlinCoreKernel::installBuiltinIntents()
{
	for (const Intent& intent : builtinIntents())
		m_intents.add(intent);
}

QJsonArray UlinCoreKernel::executableToolSchemas() const
{
	QJsonArray tools;
	for (const Intent& intent : m_intents.all()) {
		if (!intent.hasNative() && !intent.hasChain() && !intent.hasPrompt())
			continue;
		QJsonObject function{
			{ QStringLiteral("name"), intent.name },
			{ QStringLiteral("description"), intent.description },
			{ QStringLiteral("parameters"), parametersSchema(intent.parameters) }
		};
		tools.append(QJsonObject{
			{ QStringLiteral("type"), QStringLiteral("function") },
			{ QStringLiteral("function"), function }
		});
	}
	return tools;
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
	callbacks.onComplete = [this, generation](const QList<IntentCall>& calls, const QString& text,
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
	request.tools = executableToolSchemas();

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

void UlinCoreKernel::handleComplete(int generation, const QList<IntentCall>& calls, const QString& text,
	const QString& thinking, const Usage& usage, const QString& finishReason)
{
	Q_UNUSED(finishReason)
	if (generation != m_generation || !m_running)
		return;

	++m_steps;
	m_turnUsage.add(usage);
	m_session->addUsage(usage);
	emit usageChanged(m_turnUsage);

	if (m_generationPending)
		m_generationCalls.append(calls);

	const QString fullText = text.isEmpty() ? m_pendingText : text;
	const QString fullThinking = thinking.isEmpty() ? m_pendingThinking : thinking;

	Message assistant = Message::assistant(fullText, fullThinking);
	assistant.intentCalls = calls;
	if (!fullText.isEmpty() || !calls.isEmpty()) {
		m_session->append(assistant);
		emit assistantMessage(assistant);
		journal(QStringLiteral("assistant/message"), QJsonObject{
			{ QStringLiteral("turnId"), m_turnId },
			{ QStringLiteral("text"), fullText },
			{ QStringLiteral("thinking"), fullThinking },
			{ QStringLiteral("intentCalls"), static_cast<int>(calls.size()) }
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

	callIntent(generation, calls, 0);
}

void UlinCoreKernel::callIntent(int generation, const QList<IntentCall>& calls, int index)
{
	if (generation != m_generation || !m_running || m_cancelled)
		return;
	if (index >= calls.size()) {
		startStep(generation);
		return;
	}

	const IntentCall call = calls.at(index);
	emit toolStarted(m_turnId, call.id, call.name, call.arguments);
	journal(QStringLiteral("tool/call"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), call.id },
		{ QStringLiteral("name"), call.name },
		{ QStringLiteral("arguments"), call.arguments },
		{ QStringLiteral("metadata"), call.metadata }
	});

	QStringList tags;
	const QJsonValue tagsValue = call.metadata.value(QStringLiteral("tags"));
	if (tagsValue.isArray()) {
		for (const QJsonValue& tag : tagsValue.toArray()) {
			if (tag.isString())
				tags.append(tag.toString());
		}
	}
	const bool defineOnly = tags.contains(QStringLiteral("defineOnly"));
	const bool asPrompt = tags.contains(QStringLiteral("asPrompt"));
	const bool asSkill = tags.contains(QStringLiteral("asSkill"));
	const bool withSkill = tags.contains(QStringLiteral("withSkill"));
	const bool generateOnly = tags.contains(QStringLiteral("generateOnly"));
	const bool hasSaveTag = tags.contains(QStringLiteral("save"));
	const bool agentTag = tags.contains(QStringLiteral("agentEngaged"))
		|| tags.contains(QStringLiteral("skillRequired"))
		|| generateOnly;
	if (agentTag) {
		completeIntentCall(generation, calls, index, call,
			IntentResult::failure(QStringLiteral("agent cannot request agentEngaged/skillRequired")));
		return;
	}
	if ((asPrompt || asSkill || withSkill) && hasSaveTag) {
		completeIntentCall(generation, calls, index, call,
			IntentResult::failure(QStringLiteral("save cannot be combined with execution tags")));
		return;
	}
	if (defineOnly && !hasSaveTag) {
		completeIntentCall(generation, calls, index, call,
			IntentResult::failure(QStringLiteral("defineOnly requires save")));
		return;
	}

	Intent intent;
	const QJsonValue definitionValue = call.metadata.value(QStringLiteral("Intent"));
	if (definitionValue.isObject()) {
		if (asPrompt || asSkill || withSkill) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("Intent definition cannot use execution tags")));
			return;
		}
		intent = Intent::fromJson(definitionValue.toObject());
		intent.name = call.name;
		if (intent.name.trimmed().isEmpty()) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("intent definition requires a name")));
			return;
		}
		if (!intent.hasPrompt() && !intent.hasSkill() && !intent.hasChain() && !intent.hasNative()) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("intent %1 definition is empty").arg(call.name)));
			return;
		}
		addIntent(intent, hasSaveTag);
		if (defineOnly) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::success(QStringLiteral("intent %1 saved").arg(call.name)));
			return;
		}
		if (!intent.hasChain() && !intent.hasNative()) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("intent %1 has no executable form; use defineOnly").arg(call.name)));
			return;
		}
	} else {
		if (defineOnly) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("defineOnly requires Intent:{...}")));
			return;
		}
		intent = m_intents.find(call.name);
	}
	if (intent.name.isEmpty()) {
		completeIntentCall(generation, calls, index, call,
			IntentResult::failure(QStringLiteral("unknown intent: %1").arg(call.name)));
		return;
	}

	if (asPrompt || asSkill || withSkill) {
		const QJsonObject params = effectiveParameters(intent, call.arguments);
		QString prompt = call.metadata.value(QStringLiteral("prompt")).toString();
		QString skill = call.metadata.value(QStringLiteral("skill")).toString();
		if (withSkill) {
			if (!prompt.isEmpty()) {
				completeIntentCall(generation, calls, index, call,
					IntentResult::failure(QStringLiteral("withSkill rejects external prompt")));
				return;
			}
			if (!asPrompt && (intent.hasChain() || intent.hasNative())) {
				completeIntentCall(generation, calls, index, call,
					IntentResult::failure(QStringLiteral("withSkill needs asPrompt for chain/native")));
				return;
			}
			prompt = intent.prompt;
			if (skill.isEmpty())
				skill = intent.skill;
		} else if (asSkill) {
			if (prompt.trimmed().isEmpty()) {
				completeIntentCall(generation, calls, index, call,
					IntentResult::failure(QStringLiteral("asSkill requires an external prompt")));
				return;
			}
			if (skill.isEmpty())
				skill = intent.skill;
		} else {
			if (!prompt.isEmpty() || !skill.isEmpty()) {
				completeIntentCall(generation, calls, index, call,
					IntentResult::failure(QStringLiteral("asPrompt rejects external prompt/skill")));
				return;
			}
			prompt = intent.prompt;
			skill.clear();
		}

		if (prompt.trimmed().isEmpty()) {
			completeIntentCall(generation, calls, index, call,
				IntentResult::failure(QStringLiteral("intent %1 has no prompt").arg(call.name)));
			return;
		}
		QString content = expandPromptTemplate(prompt, params);
		if (asSkill || withSkill) {
			if (skill.trimmed().isEmpty()) {
				completeIntentCall(generation, calls, index, call,
					IntentResult::failure(QStringLiteral("intent %1 has no skill").arg(call.name)));
				return;
			}
			content += QStringLiteral("\n\n[Skill]\n") + expandPromptTemplate(skill, params);
		}
		completeIntentCall(generation, calls, index, call, IntentResult::success(content));
		return;
	}

	if (!intent.hasChain() && !intent.hasNative() && intent.hasPrompt()) {
		const QJsonObject params = effectiveParameters(intent, call.arguments);
		completeIntentCall(generation, calls, index, call,
			IntentResult::success(expandPromptTemplate(intent.prompt, params)));
		return;
	}

	const ExecutorContext context = m_options.toolContext;
	QPointer<UlinCoreKernel> self(this);
	auto resolver = [this](const QString& name) { return m_intents.find(name); };
	auto executor = std::make_shared<UlinCoreIntentCaller>(intent, &m_executors, resolver);
	m_toolPool->start(new ToolRunnable(
		[self, generation, calls, index, call, executor, context]() {
			const IntentResult result = executor->call(call.arguments, context);
			if (!self)
				return;
			QMetaObject::invokeMethod(self.data(),
				[self, generation, calls, index, call, result]() {
					if (UlinCoreKernel* kernel = self.data())
						kernel->completeIntentCall(generation, calls, index, call, result);
				},
				Qt::QueuedConnection);
		}));
}

void UlinCoreKernel::completeIntentCall(int generation, const QList<IntentCall>& calls, int index,
	const IntentCall& call, const IntentResult& result)
{
	if (generation != m_generation || !m_running || m_cancelled)
		return;

	QString content = result.ok
		? result.content
		: (result.error.isEmpty() ? QStringLiteral("tool failed") : result.error);
	if (content.size() > m_options.maxIntentResultChars) {
		const int dropped = content.size() - m_options.maxIntentResultChars;
		content = content.left(m_options.maxIntentResultChars)
			+ QStringLiteral("\n...[truncated %1 chars]").arg(dropped);
	}

	IntentResult stored = result;
	stored.content = content;

	m_session->append(Message::intentResult(call.id, call.name, content, !result.ok));
	journal(QStringLiteral("tool/result"), QJsonObject{
		{ QStringLiteral("turnId"), m_turnId },
		{ QStringLiteral("callId"), call.id },
		{ QStringLiteral("name"), call.name },
		{ QStringLiteral("ok"), result.ok },
		{ QStringLiteral("content"), content }
	});
	emit toolFinished(m_turnId, call.id, call.name, stored);

	callIntent(generation, calls, index + 1);
}

void UlinCoreKernel::finishPendingGeneration(bool ok)
{
	if (!m_generationPending)
		return;
	m_generationPending = false;
	const bool save = m_generationSave;
	const QString name = m_generationName;
	const QString prompt = m_generationPrompt;
	const QString skill = m_generationSkill;
	const QList<IntentCall> calls = m_generationCalls;
	m_generationSave = false;
	m_generationName.clear();
	m_generationPrompt.clear();
	m_generationSkill.clear();
	m_generationCalls.clear();

	if (!ok || name.trimmed().isEmpty())
		return;

	Intent generated;
	generated.name = name;
	generated.description = QStringLiteral("Generated intent");
	generated.prompt = prompt;
	generated.skill = skill;
	for (const IntentCall& call : calls) {
		if (call.name.trimmed().isEmpty())
			continue;
		IntentStep step;
		step.intent = call.name;
		step.arguments = call.arguments;
		generated.steps.append(step);
	}
	addIntent(generated, save);
}

void UlinCoreKernel::finishTurn(int generation, bool ok, const QString& error,
	const QString& finalText, bool cancelled)
{
	if (generation != m_generation)
		return;
	finishPendingGeneration(ok && error.isEmpty() && !cancelled);
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
