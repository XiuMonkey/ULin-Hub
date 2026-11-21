#include "common/agent/ulincore/tool/UlinCoreRecipeTool.h"

#include "common/agent/ulincore/intent/UlinCoreBinding.h"

#include <QStringList>

#include <utility>

namespace UlinCore
{
namespace
{
thread_local int g_recipeDepth = 0;
constexpr int kMaxRecipeDepth = 16;
}

UlinCoreRecipeTool::UlinCoreRecipeTool(Intent intent, UlinCoreToolRegistry* registry)
	: m_intent(std::move(intent))
	, m_registry(registry)
{
}

QString UlinCoreRecipeTool::name() const
{
	return m_intent.name;
}

QString UlinCoreRecipeTool::description() const
{
	QString text = m_intent.description;
	if (text.isEmpty())
		text = QStringLiteral("Run saved intent %1").arg(m_intent.name);
	return text;
}

QJsonObject UlinCoreRecipeTool::parametersSchema() const
{
	return UlinCore::parametersSchema(m_intent.parameters);
}

ToolResult UlinCoreRecipeTool::execute(const QJsonObject& arguments, const ToolContext& context)
{
	if (!m_registry)
		return ToolResult::failure(QStringLiteral("recipe tool has no registry"));

	// Native Intent 是叶子执行器：直接执行 executor，不再走 steps。
	if (m_intent.hasNative()) {
		QString executor = m_intent.executor;
		if (executor.startsWith(QStringLiteral("builtin.")))
			executor = executor.mid(8);
		if (executor == m_intent.name)
			return ToolResult::failure(QStringLiteral("intent %1 cannot execute itself").arg(m_intent.name));
		if (g_recipeDepth >= kMaxRecipeDepth)
			return ToolResult::failure(QStringLiteral("recipe recursion limit reached"));
		const std::shared_ptr<UlinCoreTool> tool = m_registry->findShared(executor);
		if (!tool)
			return ToolResult::failure(QStringLiteral("unknown executor in intent %1: %2")
				.arg(m_intent.name, m_intent.executor));
		++g_recipeDepth;
		const ToolResult result = tool->execute(arguments, context);
		--g_recipeDepth;
		return result;
	}

	if (m_intent.steps.isEmpty())
		return ToolResult::failure(QStringLiteral("intent %1 has no executable chain").arg(m_intent.name));
	if (g_recipeDepth >= kMaxRecipeDepth)
		return ToolResult::failure(QStringLiteral("recipe recursion limit reached"));

	++g_recipeDepth;
	QStringList output;
	for (const IntentStep& step : m_intent.steps) {
		const QJsonObject stepArgs = resolveBindings(step.arguments, arguments);
		const std::shared_ptr<UlinCoreTool> tool = m_registry->findShared(step.intent);
		if (!tool) {
			--g_recipeDepth;
			return ToolResult::failure(QStringLiteral("unknown intent in recipe %1: %2")
				.arg(m_intent.name, step.intent));
		}
		const ToolResult result = tool->execute(stepArgs, context);
		if (!result.ok) {
			--g_recipeDepth;
			return ToolResult::failure(result.error.isEmpty()
				? QStringLiteral("step failed: %1").arg(step.intent)
				: result.error);
		}
		if (!result.content.isEmpty())
			output.append(result.content);
	}
	--g_recipeDepth;

	const QString content = output.join(QLatin1Char('\n'));
	return ToolResult::success(content.isEmpty() ? QStringLiteral("ok") : content);
}
} // namespace UlinCore
