#include "common/agent/ulincore/intent/UlinCoreIntentCaller.h"

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

UlinCoreIntentCaller::UlinCoreIntentCaller(Intent intent, UlinCoreNativeRegistry* executors,
	IntentResolver resolver)
	: m_intent(std::move(intent))
	, m_executors(executors)
	, m_resolve(std::move(resolver))
{
}

IntentResult UlinCoreIntentCaller::call(const QJsonObject& arguments, const ExecutorContext& context)
{
	if (g_executorDepth >= kMaxExecutorDepth)
		return IntentResult::failure(QStringLiteral("intent execution recursion limit reached"));
	if (!m_executors)
		return IntentResult::failure(QStringLiteral("intent executor has no executor registry"));

	if (m_intent.hasNative()) {
		const QString original = m_intent.executor;
		// 只有 executor 原文等于 Intent 名时才视为自执行；
		// builtin.xxx 是独立执行器命名空间。
		if (original == m_intent.name)
			return IntentResult::failure(QStringLiteral("intent %1 cannot execute itself").arg(m_intent.name));

		// 注册表只接受完整 executor ID（内置为 builtin.xxx）。
		// 不做去前缀回退：拼错或注册错误必须立即暴露，避免与同名用户执行器混淆。
		const auto executor = m_executors->findShared(original);
		if (!executor)
			return IntentResult::failure(QStringLiteral("unknown executor in intent %1: %2")
				.arg(m_intent.name, m_intent.executor));
		if (!executor)
			return IntentResult::failure(QStringLiteral("unknown executor in intent %1: %2")
				.arg(m_intent.name, m_intent.executor));

		++g_executorDepth;
		const ExecuteResult result = executor->execute(arguments, context);
		--g_executorDepth;
		return result.ok
			? IntentResult::success(result.content, result.data)
			: IntentResult::failure(result.error);
	}

	if (m_intent.steps.isEmpty())
		return IntentResult::failure(QStringLiteral("intent %1 has no executable form").arg(m_intent.name));

	++g_executorDepth;
	QStringList output;
	for (const IntentStep& step : m_intent.steps) {
		const Intent target = m_resolve ? m_resolve(step.intent) : Intent();
		if (target.name.isEmpty()) {
			--g_executorDepth;
			return IntentResult::failure(QStringLiteral("unknown intent in recipe %1: %2")
				.arg(m_intent.name, step.intent));
		}
		UlinCoreIntentCaller nested(target, m_executors, m_resolve);
		const IntentResult result = nested.call(resolveBindings(step.arguments, arguments), context);
		if (!result.ok) {
			--g_executorDepth;
			return IntentResult::failure(result.error.isEmpty()
				? QStringLiteral("step failed: %1").arg(step.intent) : result.error);
		}
		if (!result.content.isEmpty())
			output.append(result.content);
	}
	--g_executorDepth;
	return IntentResult::success(output.isEmpty() ? QStringLiteral("ok") : output.join(QLatin1Char('\n')));
}
} // namespace UlinCore
