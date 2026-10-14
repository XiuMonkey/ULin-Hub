#pragma once

// 分层（领域归位 2026-10-15）：本文件在 common/session/ —— **领域与纯逻辑层**（模型本体：摊平算法、
// 折叠状态机、行的角色名映射都在这里）。它不 import 任何 QML 概念（只用 QtCore 的 QAbstractListModel），
// 可以脱离 QML 引擎单测（`new SessionListModel(catalog)` 之后断言行数/角色值即可）。
// QML 面（`Hub.sessions` 属性、SessionListModel 类型注册）在 qmlbridge/QmlBridge.* 与 src/core/main.cpp，
// 那边只接线、不放算法。
//
// 侧栏列表的行模型（QML 侧经 Hub.sessions 读）：把 SessionCatalog 的"工作区分组 + 会话"**摊平**成一行一行的列表。
// 为什么摊平而不是嵌套 ListView（坑）：嵌套滚动会互相抢滚轮/惯性，是性能与手感的双重坑；
// 摊平后一个 ListView 就够，行数规模是"几十行"，整份重建比增量补丁简单也不会错。
// 行有两种：工作区表头（可折叠、带会话计数）与会话行。

#include "common/session/SessionCatalog.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

class SessionListModel : public QAbstractListModel
{
	Q_OBJECT
		Q_PROPERTY(int count READ rowCount NOTIFY rebuilt)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // "workspace" | "session"
		WorkspaceIdRole,
		WorkspaceTitleRole,
		SessionCountRole,
		ExpandedRole,
		SessionIdRole,
		TitleRole,
		RunningRole,
		SelectedRole
	};

	explicit SessionListModel(SessionCatalog* catalog, QObject* parent = nullptr);

	// "未分组"表头行用的 **界面 key**（不是服务端的工作区 id）：它只在本模型里当折叠状态/行标识用。
	// 任何要发给服务端的调用（session/create 等）都必须先把它翻成"没有工作区"（空 id），
	// 原样发过去服务端会回 workspace/not-found —— 这就是"点＋新建不了会话"的根因。
	// ⚠️ 换算在核心侧做（`ClientRuntime::createSession`），侧栏只管把 key 抛上来。
	static QString ungroupedWorkspaceKey();

	// 数据变了就整份重建（调用方：控制器拉到新列表 / 新建 / 改标题 / 折叠展开）
	void rebuild();

	// 底层数据**不归本模型所有**：目录由 core/ClientRuntime 持有并更新（2026-10-10 拆分层时改的），
	// 本模型只读它、把它摊平成行。别在这里写目录 —— 那些 RPC 的回包处理都在 ClientRuntime 里。
	SessionCatalog* catalog() const { return m_catalog; }

	void setSelectedSession(const QString& sessionId);
	QString selectedSessionId() const { return m_selectedSessionId; }

	// 折叠/展开某个工作区（只影响显示，不写配置）
	Q_INVOKABLE void toggleWorkspace(const QString& workspaceId);
	Q_INVOKABLE bool isWorkspaceExpanded(const QString& workspaceId) const;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

signals:
	void rebuilt();

private:
	struct Row
	{
		QString kind;
		QString workspaceId;
		QString workspaceTitle;
		int sessionCount = 0;
		bool expanded = true;
		QString sessionId;
		QString title;
		bool running = false;
		bool selected = false;
	};

	QVector<Row> m_rows;
	QSet<QString> m_collapsed;  // 被收起来的工作区 id（默认全展开：分组不该开局就是收着的）
	QString m_selectedSessionId;
	SessionCatalog* m_catalog = nullptr;   // 不持有所有权（core/ClientRuntime 持有）
};
