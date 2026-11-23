#pragma once

// 客户端运行期：内置服务端 + 出站客户端 + 会话目录的生命周期，以及会话/工作区的 RPC 业务。
// 不含任何 QML 面向的接线（Q_PROPERTY / Q_INVOKABLE / QML 模型）；拆自旧 ShellController.cpp。
// ✅ 服务端启动/重启、DshApiClient、baseUrl 开流、状态机、SessionCatalog、会话与工作区 RPC
// ❌ QML 属性转发与列表模型；宿主 index（kApiClient / kMainWindow）登记在本类。

#include "common/session/SessionCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class DshApiClient;
class AgentSessionClient;
class ExtensionToolHost;
class ServerManager;
class QJsonArray;

namespace UlinCore { class UlinCoreMasterAgent; }

class ClientRuntime : public QObject
{
	Q_OBJECT

public:
	explicit ClientRuntime(QObject* parent = nullptr);

	// 启动期调一次（QML 装载之前）：建服务端/客户端/目录 → 登记两个宿主 index → 接线 → 起服务端
	void start();

	DshApiClient* apiClient() const { return m_api; }
	AgentSessionClient* agentSession() const { return m_agentSession; }
	ServerManager* server() const { return m_server; }
	// 扩展工具宿主（命名管道 → DllCaller）：扩展管理页装/卸工具扩展时要它 reload/unload。
	// 在内置服务端 start 之后创建；接管态下同样存在（桥与后端是不是 DSH 无关）。
	ExtensionToolHost* toolHost() const { return m_toolHost; }
	// ⚠️ 值成员（非 QObject、无 parent 机制），所有权在语义上属于本类；列表模型只拿指针读数。
	SessionCatalog* catalog() { return &m_catalog; }

	void refreshSessions();
	// 轻量重拉：只更新每个会话的 running（子代理页签的"运行中"点靠它），不重建列表、不动选中项。
	void refreshSessionStates();
	// workspaceId 传空 = 未分组（服务端用默认 cwd）；侧栏的界面 key 在这里换算，UI 不用管
	void createSession(const QString& workspaceId);
	// 清空会话（侧栏 danger 按钮）：抹掉本地会话数据与工作区注册表，再逐个 workspace/delete，
	// 删完建一个新的默认会话。与 Sidebar::clearAllSessions → deleteAllThen → callSessionCreate 同路。
	void clearAllSessions();
	void archiveSession(const QString& sessionId);
	// 还原已归档会话（workspace/unarchiveSession）
	void unarchiveSession(const QString& sessionId);
	// 彻底删除已归档会话（session/delete）
	void deleteSessionPermanently(const QString& sessionId);
	void deleteWorkspace(const QString& workspaceId);
	void createWorkspace(const QString& path);   // 目录选择在外面（QFileDialog 是控件层的事）
	void followSession(const QString& sessionId);
	void restartServer();
	// "会话被旧进程占用"自愈用：重启服务端后，重发前要重新 follow
	void refollow(const QString& sessionId);

	bool setStatus(const QString& text);
	bool setServerState(const QString& state);
	bool setInitializing(bool value);
	QString status() const { return m_status; }
	QString serverState() const { return m_serverState; }
	bool initializing() const { return m_initializing; }

	// 自愈重发的一次性状态在发送方（common/session/ChatController 的 m_pendingRetry），
	// 那里才有消息区可以插系统提示；本类只提供"重启服务端"和"重新 follow"两个动作。

signals:
	// 状态机（QmlBridge 转成 QML 属性信号）。setter 返回值 = 值是否真的变了，变了才 emit。
	void statusChanged(const QString& text);
	void serverStateChanged(const QString& state);
	void initializingChanged(bool initializing);
	// 会话被归档（带上被归档的 id：宿主据此判断"删的是不是当前会话"）
	void sessionArchived(const QString& sessionId);
	// 目录变了，宿主该 rebuild 它的列表模型
	void catalogChanged();
	// 子代理目录（会话投影 subagentCatalog）变了：顶栏"主 ↔ 子"页签据此重画
	void peerCatalogChanged();
	// 列表拉回来了；参数是服务端建议的选中项（空 = 保持现状）
	void sessionsRefreshed(const QString& autoSelectSessionId);
	// 会话被整份清空（含当前会话）：宿主据此清掉消息区与"当前会话"状态，否则旧选中项会挡掉自动选中。
	void sessionsCleared();
	// 会话/工作区操作失败（宿主转发给状态行或 Toast）
	void operationFailed(const QString& message);
	// 新建会话失败（宿主用 Toast 显示：statusText 只在初始化遮罩里可见，不弹出来就是"没反应"）
	void sessionCreateFailed(const QString& message);

private:
	// 真正发 session/create 的一步：workspaceId 是**已经解析好**的服务端 id（可能为空 = 确实没分组可挂）
	void createSessionIn(const QString& workspaceId);
	// 新会话该挂哪个分组：基线里已有分组就用第一个；一个都没有就按内置服务端的 cwd 补建默认分组；
	// 解析结果可能为空串（服务端不可用 / 补建失败）—— 那表示只能落到未分组。
	void resolveSessionWorkspace(const std::function<void(const QString&)>& onResolved);
	// 把点名的每个工作区都对服务端 workspace/delete 一次，删完（或超时兜底）再回调。
	// id 必须由调用方在清本地目录之前取好；先删完再建，否则新建分组可能被随后的 delete 抹掉。
	void deleteWorkspacesThen(const QStringList& workspaceIds, const std::function<void()>& onDone);
	// 逐个归档会话（workspace/archiveSession），全部收齐或 3 秒兜底后回调。
	// 删除工作区前必须先走这一步：workspace/delete 只删注册、不动会话，否则会话会变成无归属。
	// notifyHost = false：不逐条发 sessionArchived（"清空全部"由 sessionsCleared 统一处理）。
	void archiveSessionsThen(const QStringList& sessionIds, const std::function<void()>& onDone, bool notifyHost = true);

	ServerManager* m_server = nullptr;
	DshApiClient* m_api = nullptr;
	AgentSessionClient* m_agentSession = nullptr;
	UlinCore::UlinCoreMasterAgent* m_ulinCoreAgent = nullptr;
	ExtensionToolHost* m_toolHost = nullptr;
	SessionCatalog m_catalog;   // 值成员（见 catalog() 的说明）

	QString m_status;
	QString m_serverState;
	bool m_initializing = true;
	// 正在退出：服务端"finished"信号是退出路径自己杀出来的，别再当成"启动失败"报警
	bool m_exiting = false;
};
