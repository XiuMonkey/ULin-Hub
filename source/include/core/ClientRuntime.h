#pragma once

// ClientRuntime —— 客户端运行期：内置服务端 + 出站客户端 + 会话目录的生命周期与 RPC 业务。
// **不含任何 QML 面向的接线**（Q_PROPERTY / Q_INVOKABLE / QML 模型都不在这里）。
//
// 拆自 src/qmlbridge/ShellController.cpp（2026-10-10）。原来那个类三件事混在一起：QML 单例的
// 属性与转发（接线）、服务端与 API 客户端生命周期、会话/工作区的 RPC 业务。本类只承担后两者。
//
// 职责边界（改这里之前先问"这是不是接线"）：
//   ✅ 建/起/重启内置服务端、建 DshApiClient、把 baseUrl 交出去并开流、退出时关流、
//      状态机（statusText / serverState / initializing）、拥有 SessionCatalog、
//      会话与工作区的 RPC（刷新列表 / 新建会话 / 归档会话 / 删工作区 / 建工作区 / follow）
//   ❌ QML 面向的属性与转发、任何 QML 模型（SessionListModel / MessageListModel）
//
// 宿主 index 登记也在本类（不是接线，是"谁被登记"的归属）：kApiClient 与 kMainWindow 都指向
// apiClient()。2026-10-01 补：此前这两个 index 从没登记过 ⇒ 接管型扩展拿不到出站客户端。

#include "common/session/SessionCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class DshApiClient;
class ServerManager;
class QJsonArray;

class ClientRuntime : public QObject
{
	Q_OBJECT

public:
	explicit ClientRuntime(QObject* parent = nullptr);

	// 启动期调一次（QML 装载之前）：建服务端/客户端/目录 → 登记两个宿主 index → 接线 → 起服务端
	void start();

	DshApiClient* apiClient() const { return m_api; }
	ServerManager* server() const { return m_server; }
	// ⚠️ SessionCatalog 是**值成员**（它不是 QObject、没有 parent 机制），但所有权在语义上属于本类：
	//    qmlbridge/SessionListModel 只拿指针来读数（见那边的构造参数）。
	SessionCatalog* catalog() { return &m_catalog; }

	// ---- 会话 / 工作区 RPC ----
	void refreshSessions();
	// workspaceId 传空 = 未分组（服务端用默认 cwd）；侧栏的界面 key 在这里换算，UI 不用管
	void createSession(const QString& workspaceId);
	// 清空会话（侧栏那颗 danger 按钮）：抹掉本地会话数据（sessions 目录 + storages/workspace.json）与本地
	// 目录，再把服务端注册表里的每个工作区逐个 workspace/delete 删掉，删完建一个新的默认会话。与旧壳
	// DSHHub::onClearConversationClicked 同路（Sidebar::clearAllSessions → deleteAllThen → callSessionCreate）。
	void clearAllSessions();
	void archiveSession(const QString& sessionId);
	void deleteWorkspace(const QString& workspaceId);
	void createWorkspace(const QString& path);   // 目录选择在外面（QFileDialog 是控件层的事）
	void followSession(const QString& sessionId);
	void restartServer();
	// "会话被旧进程占用"自愈用：重启服务端后，重发前要重新 follow
	void refollow(const QString& sessionId);

	// ---- 状态机（QmlBridge 把它转成 QML 属性信号）----
	// 返回"值是否真的变了"：QML 属性不能无脑 emit，去重在这里做
	bool setStatus(const QString& text);
	bool setServerState(const QString& state);
	bool setInitializing(bool value);
	QString status() const { return m_status; }
	QString serverState() const { return m_serverState; }
	bool initializing() const { return m_initializing; }

	// 自愈重发的一次性状态在发送方（common/session/ChatController 的 m_pendingRetry）——
	// 那里才有消息区可以插系统提示。本类只提供"重启服务端"和"重新 follow"两个动作。

signals:
	// 状态机（QmlBridge 转成 QML 属性信号）。setter 返回值 = 值是否真的变了，变了才 emit。
	void statusChanged(const QString& text);
	void serverStateChanged(const QString& state);
	void initializingChanged(bool initializing);
	// 会话被归档（带上被归档的 id：宿主据此判断"删的是不是当前会话"）
	void sessionArchived(const QString& sessionId);
	// 目录变了，宿主该 rebuild 它的列表模型
	void catalogChanged();
	// 列表拉回来了；参数是服务端建议的选中项（空 = 保持现状）
	void sessionsRefreshed(const QString& autoSelectSessionId);
	// 会话被整份清空（含当前会话）：宿主据此清掉消息区与"当前会话"这类界面状态 —— 不等它，
	// 旧选中项会把随后那次自动选中挡掉（见 QmlBridge 的 sessionsRefreshed 处理）。
	void sessionsCleared();
	// 会话/工作区操作失败（宿主转发给状态行或 Toast）
	void operationFailed(const QString& message);
	// 新建会话失败（宿主用 Toast 显示：statusText 只在初始化遮罩里可见，不弹出来就是"没反应"）
	void sessionCreateFailed(const QString& message);

private:
	// 真正发 session/create 的一步：workspaceId 是**已经解析好**的服务端 id（可能为空 = 确实没分组可挂）
	void createSessionIn(const QString& workspaceId);
	// 新会话该挂哪个分组：基线里已有分组就用第一个，一个都没有就按内置服务端的 cwd 补建默认分组；
	// 解析结果可能为空串（服务端不可用 / 补建失败）—— 那表示只能落到未分组。
	void resolveSessionWorkspace(const std::function<void(const QString&)>& onResolved);
	// 把点名的每个工作区都对服务端 workspace/delete 一次，删完（或超时兜底）再回调。id 必须由调用方
	// **在清本地目录之前**取好传进来。清空会话之所以要先删完再建：反过来的话 workspace/create 可能被
	// 随后到达的 delete 抹掉，新会话就挂在一个已删除的分组上了。
	void deleteWorkspacesThen(const QStringList& workspaceIds, const std::function<void()>& onDone);

	ServerManager* m_server = nullptr;
	DshApiClient* m_api = nullptr;
	SessionCatalog m_catalog;   // 值成员（见 catalog() 的说明）

	QString m_status;
	QString m_serverState;
	bool m_initializing = true;
};
