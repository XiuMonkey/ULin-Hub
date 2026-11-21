#include "common/agent/ulincore/executor/UlinCoreIntentExecutor.h"

#include "common/agent/ulincore/intent/UlinCoreBinding.h"

#include <QStringList>

#include <utility>

namespace UlinCore
{
namespace
{
thread_local int g_executorDepth = 0;
constexpr int kMaxExecutorDepth = 32;
}

UlinCoreIntentExecutor::UlinCoreIntentExecutor(Intent intent, UlinCoreExecutorRegistry* executors,
	IntentResolver resolver)
	: m_intent(std::move(intent))
	, m_executors(executors)
	, m_resolve(std::move(resolver))
{
}

ToolResult UlinCoreIntentExecutor::execute(const QJsonObject& arguments, const ExecutorContext& context)
{
	if (g_executorDepth >= kMaxExecutorDepth)
		return ToolResult::failure(QStringLiteral("intent execution recursion limit reached"));
	if (!m_executors)
		return ToolResult::failure(QStringLiteral("intent executor has no executor registry"));

	if (m_intent.hasNative()) {
		QString id = m_intent.executor;
		if (id.startsWith(QStringLiteral("builtin.")))
			id = id.mid(8);
		if (id == m_intent.name)
			return ToolResult::failure(QStringLiteral("intent %1 cannot execute itself").arg(m_intent.name));
		const auto executor = m_executors->findShared(id);
		if (!executor)
			return ToolResult::failure(QStringLiteral("unknown executor in intent %1: %2")
				.arg(m_intent.name, m_intent.executor));

		++g_executorDepth;
		const ToolResult result = executor->execute(arguments, context);
		--g_executorDepth;
		return result;
	}

	if (m_intent.steps.isEmpty())
		return ToolResult::failure(QStringLiteral("intent %1 has no executable form").arg(m_intent.name));

	++g_executorDepth;
	QStringList output;
	for (const IntentStep& step : m_intent.steps) {
		const Intent target = m_resolve ? m_resolve(step.intent) : Intent();
		if (target.name.isEmpty()) {
			--g_executorDepth;
			return ToolResult::failure(QStringLiteral("unknown intent in recipe %1: %2")
				.arg(m_intent.name, step.intent));
		}
		UlinCoreIntentExecutor nested(target, m_executors, m_resolve);
		const ToolResult result = nested.execute(resolveBindings(step.arguments, arguments), context);
		if (!result.ok) {
			--g_executorDepth;
			return ToolResult::failure(result.error.isEmpty()
				? QStringLiteral("step failed: %1").arg(step.intent) : result.error);
		}
		if (!result.content.isEmpty())
			output.append(result.content);
	}
	--g_executorDepth;
	return ToolResult::success(output.isEmpty() ? QStringLiteral("ok") : output.join(QLatin1Char('\n')));
}
} // namespace UlinCore
