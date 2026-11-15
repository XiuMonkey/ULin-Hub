#include "common/agent/ulincore/UlinCoreEchoModel.h"

#include <QJsonObject>
#include <QTimer>

namespace UlinCore
{
UlinCoreEchoModel::UlinCoreEchoModel(QObject* parent)
	: UlinCoreModel(parent)
{
}

void UlinCoreEchoModel::setScriptedToolCall(const QString& name, const QJsonObject& arguments)
{
	m_scriptName = name;
	m_scriptArgs = arguments;
	m_scriptConsumed = false;
}

void UlinCoreEchoModel::clearScriptedToolCall()
{
	m_scriptName.clear();
	m_scriptArgs = QJsonObject();
	m_scriptConsumed = false;
}

void UlinCoreEchoModel::start(const ModelRequest& request, const ModelCallbacks& callbacks)
{
	m_request = request;
	m_callbacks = callbacks;
	m_cancelled = false;
	const int generation = ++m_generation;

	QTimer::singleShot(0, this, [this, generation] {
		if (m_cancelled || generation != m_generation)
			return;

		// 第一步按脚本发一次工具调用，让内核跑通 tool → model 回路。
		if (!m_scriptConsumed && !m_scriptName.isEmpty()) {
			m_scriptConsumed = true;
			ToolCall call;
			call.id = QStringLiteral("echo-call-1");
			call.name = m_scriptName;
			call.arguments = m_scriptArgs;
			if (m_callbacks.onComplete)
				m_callbacks.onComplete({ call }, QString(), QString(), Usage(), QStringLiteral("tool_calls"));
			return;
		}

		QString text;
		for (int i = m_request.messages.size() - 1; i >= 0; --i) {
			if (m_request.messages.at(i).role == Role::User) {
				text = m_request.messages.at(i).text;
				break;
			}
		}

		const QString reply = QStringLiteral("echo: ") + text;
		if (m_callbacks.onDelta)
			m_callbacks.onDelta(reply, QString());
		if (m_callbacks.onComplete)
			m_callbacks.onComplete({}, reply, QString(), Usage(), QStringLiteral("stop"));
	});
}

void UlinCoreEchoModel::cancel()
{
	m_cancelled = true;
	++m_generation;
}
} // namespace UlinCore
