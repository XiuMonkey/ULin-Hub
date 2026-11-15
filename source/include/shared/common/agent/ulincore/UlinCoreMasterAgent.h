#pragma once

#include "common/agent/ulincore/UlinCoreKernel.h"

#include "VirtualClass/VirtualApiTakeover.h"
#include "VirtualClass/VirtualMasterAgent.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <memory>

class VirtualMain;

namespace UlinCore
{
// 把自研内核挂到 Agent 注册表上（和 CodexBackend / WorkBuddy 同一张表）：
//   · AgentManager 通过 VirtualMasterAgent 取 id / name / 生命周期；
//   · 激活后接管出站 API，session/prompt 由本类驱动 UlinCoreKernel；
//   · 内核信号翻成宿主认识的 session/event 帧（assistant/chunk、tool/call…）。
//
// 这是 DSH 协议适配层，不是内核本身：内核仍然只认 UlinCore::Message / ToolCall。
class UlinCoreMasterAgent : public QObject, public VirtualMasterAgent, public VirtualApiSink
{
	Q_OBJECT
	Q_INTERFACES(VirtualMasterAgent VirtualApiSink)

public:
	UlinCoreMasterAgent(VirtualApiHost* host, VirtualMain* main, QObject* parent = nullptr);
	~UlinCoreMasterAgent() override;

	// 不注入时默认用 UlinCoreEchoModel（只验证回路，不是生产模型）。
	void setModel(std::shared_ptr<UlinCoreModel> model);
	void setWorkspaceRoot(const QString& root);
	// <exe>/data/ulincore：会话/分组落盘在这里（sessions.json）。
	void setDataDir(const QString& dir);

	const char* MasterAgentId() const override { return "ulincore"; }
	const char* MasterAgentName() const override { return "ULin Core"; }
	const char* MasterAgentDescription() const override
	{
		return "自研 Agent 内核（ulincore）：本地消息账本、工具执行与回合状态机";
	}

	void ActivateMasterAgent() override;
	void DeactivateMasterAgent() override;
	void OnOutboundRequest(const char* rpcId, const char* method, const char* argsJson) override;

private:
	struct SessionRecord
	{
		QString id;
		QString title;
		QString workspaceId;
		QString cwd;
		qint64 createdAt = 0;
		qint64 updatedAt = 0;
		QJsonArray events;   // 本地账本：turn/start、user/message、assistant/message、tool/*、turn/end
		int nextSeq = 1;
	};

	struct WorkspaceRecord
	{
		QString id;
		QString title;
		QString path;
	};

	void ensureKernel();
	void loadSettings();
	void reloadSettings();
	QString createSessionRecord(const QString& workspaceId, const QString& cwd);
	QJsonObject sessionListResult() const;
	QString sessionLogPath(const QString& sessionId) const;
	void loadSessionLog(SessionRecord& record) const;
	void saveSessionLog(const SessionRecord& record) const;
	void appendSessionEvent(const QString& type, const QJsonObject& data = QJsonObject());
	void forwardWorkspaceSnapshot();
	void loadState();
	void saveState() const;
	void forwardEvent(const QString& type, const QJsonObject& data);
	void forwardText(const QString& text, const QString& thinking);
	void forwardToolCall(const QString& name, const QJsonObject& arguments);
	void forwardToolResult(const QString& content, bool ok);
	void forwardFullMessage(const Message& message);
	QJsonArray messageContent(const Message& message) const;

	VirtualApiHost* m_host = nullptr;
	VirtualMain* m_main = nullptr;
	UlinCoreKernel* m_kernel = nullptr;
	std::shared_ptr<UlinCoreModel> m_model;
	QString m_sessionId;
	QString m_workspaceRoot;
	QString m_dataDir;
	QString m_systemPrompt;
	QHash<QString, SessionRecord> m_sessions;
	QHash<QString, WorkspaceRecord> m_workspaces;
	bool m_active = false;
	bool m_streamedThisTurn = false;
	bool m_modelExplicit = false;
	bool m_settingsConnected = false;
};
} // namespace UlinCore
