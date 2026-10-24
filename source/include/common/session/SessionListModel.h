#pragma once

// 分层：本文件在 common/session/ —— **领域与纯逻辑层**（模型本体：摊平算法、
// 折叠状态机、行的角色名映射都在这里）。它不 import 任何 QML 概念（只用 QtCore 的 QAbstractListModel），
// 可以脱离 QML 引擎单测（`new SessionListModel(catalog)` 之后断言行数/角色值即可）。
// QML 面（`Hub.sessions` 属性、SessionListModel 类型注册）在 qmlbridge/QmlBridge.* 与 src/core/main.cpp，
// 那边只接线、不放算法。
// 侧栏列表的行模型（QML 侧经 Hub.sessions 读）：把 SessionCatalog 的"工作区分组 + 会话"**摊平**成一行一行的列表。
// 为什么摊平而不是嵌套 ListView（坑）：嵌套滚动会互相抢滚轮/惯性，是性能与手感的双重坑；
// 摊平后一个 ListView 就够，行数规模是"几十行"，整份重建比增量补丁简单也不会错。
// 行有三种：工作区表头（可折叠、带会话计数）、"置顶"表头（置顶段，同样可折叠），以及会话行。
// 会话行可能出现在两处：置顶段一份、它所属的工作区分组一份 —— 这是设计上的有意重复（置顶段是快捷入口，
// 工作区分组仍保持完整），两处都指向同一个 sessionId，选中态同步。

#include "common/session/SessionCatalog.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVector>

class SessionListModel : public QAbstractListModel
{
	Q_OBJECT
		Q_PROPERTY(int count READ rowCount NOTIFY rebuilt)
		// 侧栏搜索框的文本：空串 = 不过滤；非空 = 大小写不敏感匹配会话标题，未命中的会话/分组隐藏。
		// 置顶段与各工作区分组都跟着它一起重建（行数只有几十，整份重建最简单、也不会和折叠态打架）。
		Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY rebuilt)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // "workspace" | "pinned" | "session"
		WorkspaceIdRole,
		WorkspaceTitleRole,
		SessionCountRole,
		ExpandedRole,
		SessionIdRole,
		TitleRole,
		RunningRole,
		SelectedRole,
		UpdatedAtRole    // qint64：会话最近活动时间（epoch ms），0 = 未知（QML 里读 model.updatedAtMs）
	};

	explicit SessionListModel(SessionCatalog* catalog, QObject* parent = nullptr);

	// "未分组"表头行用的 **界面 key**（不是服务端的工作区 id）：它只在本模型里当折叠状态/行标识用。
	// 任何要发给服务端的调用（session/create 等）都必须先把它翻成"没有工作区"（空 id），
	// 原样发过去服务端会回 workspace/not-found —— 这就是"点＋新建不了会话"的根因。
	// ⚠️ 换算在核心侧做（`ClientRuntime::createSession`），侧栏只管把 key 抛上来。
	static QString ungroupedWorkspaceKey();
	// "置顶"表头行用的界面 key（同样不是服务端实体；服务端只有 pinnedSessionIds 集合）。
	// 它只用于折叠状态/行标识，任何 RPC 都不能把它当 workspaceId 发出去。
	static QString pinnedWorkspaceKey();

	// 数据变了就整份重建（调用方：控制器拉到新列表 / 新建 / 改标题 / 折叠展开）
	void rebuild();

	QString filterText() const { return m_filterText; }
	// 搜索过滤：空串恢复全部；非空时大小写不敏感匹配会话标题（标题为空回退到会话 id，与 TitleRole 一致）。
	// Q_INVOKABLE：搜索框在 QML 里直接 `Hub.sessions.setFilterText(text)`。
	Q_INVOKABLE void setFilterText(const QString& text);

	// 底层数据**不归本模型所有**：目录由 core/ClientRuntime 持有并更新，
	// 本模型只读它、把它摊平成行。别在这里写目录 —— 那些 RPC 的回包处理都在 ClientRuntime 里。
	SessionCatalog* catalog() const { return m_catalog; }

	void setSelectedSession(const QString& sessionId);
	QString selectedSessionId() const { return m_selectedSessionId; }

	// 折叠/展开某个工作区（只影响显示，不写配置）
	Q_INVOKABLE void toggleWorkspace(const QString& workspaceId);
	Q_INVOKABLE bool isWorkspaceExpanded(const QString& workspaceId) const;
	// 输入框上下文带的工作区切换菜单：只返回"工作区表头"行（不含置顶/会话），
	// 每项 { workspaceId, title, count, ungrouped }，供 QML Repeater 直接使用。
	Q_INVOKABLE QVariantList workspaceItems() const;
	// 切到某工作区时优先打开它最近的一条会话；没有会话则返回空串（由 QML 决定新建）。
	Q_INVOKABLE QString firstSessionForWorkspace(const QString& workspaceId) const;

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
		qint64 updatedAtMs = 0;
	};

	// 会话行内容（置顶段 / 工作区分组 / 未分组共用）：只差选中态；置顶归属由所在段决定。
	Row makeSessionRow(const SessionRecord& record) const;
	// 表头行内容（工作区 / 置顶 / 未分组共用）：expanded 统一按 workspaceId 查折叠状态
	Row makeHeaderRow(const char* kind, const QString& workspaceId, const QString& workspaceTitle,
		int sessionCount) const;

	QVector<Row> m_rows;
	QSet<QString> m_collapsed;  // 被收起来的工作区 id（默认全展开：分组不该开局就是收着的）
	QString m_filterText;       // 搜索过滤文本，空 = 不过滤（见 setFilterText）
	QString m_selectedSessionId;
	SessionCatalog* m_catalog = nullptr;   // 不持有所有权（core/ClientRuntime 持有）
};
