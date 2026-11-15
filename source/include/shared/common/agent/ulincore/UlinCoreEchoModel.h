#pragma once

#include "common/agent/ulincore/UlinCoreModel.h"

namespace UlinCore
{
// 无网络的确定性模型：用来跑通内核回路 / 单测。
// 首步可选发一次脚本化工具调用，之后回显最后一条用户消息。
class UlinCoreEchoModel : public UlinCoreModel
{
	Q_OBJECT

public:
	explicit UlinCoreEchoModel(QObject* parent = nullptr);

	QString name() const override { return QStringLiteral("echo"); }
	void start(const ModelRequest& request, const ModelCallbacks& callbacks) override;
	void cancel() override;

	void setScriptedToolCall(const QString& name, const QJsonObject& arguments);
	void clearScriptedToolCall();

private:
	ModelCallbacks m_callbacks;
	ModelRequest m_request;
	bool m_cancelled = false;
	int m_generation = 0;
	bool m_scriptConsumed = false;
	QString m_scriptName;
	QJsonObject m_scriptArgs;
};
} // namespace UlinCore
