#include "common/agent/dsh/DshMasterAgent.h"

#include <QDebug>

DshMasterAgent::DshMasterAgent(VirtualApiHost* host, QObject* parent)
	: QObject(parent)
	, m_host(host)
{
}

void DshMasterAgent::ActivateMasterAgent()
{
	// 内置 DSH 的传输在客户端启动时已经就绪，切换到这里只是切回原生路径。
	qInfo().noquote() << "[DshMasterAgent] activated (builtin native transport)";
}

void DshMasterAgent::DeactivateMasterAgent()
{
	// 不停止内置 DSH 服务；是否重新连接由 DshApiClient / ServerManager 决定。
	qInfo().noquote() << "[DshMasterAgent] deactivated";
}

void DshMasterAgent::OnOutboundRequest(const char* rpcId, const char* method, const char* argsJson)
{
	Q_UNUSED(argsJson);
	const QByteArray id = rpcId ? QByteArray(rpcId) : QByteArray();
	qWarning().noquote() << "[DshMasterAgent] unexpected takeover request for builtin dsh:"
		<< (method ? method : "") << "rpcId=" << id;
	if (m_host)
		m_host->FailCall(rpcId, "dsh-native", "builtin DSH does not provide a takeover sink");
}
