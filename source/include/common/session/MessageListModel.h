#pragma once

// 会话消息列表的 QML 模型：一行 = 一条消息（用户 / Agent / 系统提示）。
// Agent 消息内部是**有序段**（thinking / reply / toolCall / toolResult），与旧壳 AgentMessageUnit::StreamSegment 一一对应，
// "思考、工具调用、回复交错出现"的顺序不会丢；reply 段按代码围栏切成单元（MarkdownSegmenter）。
// 更新策略（坑）：段是追加式的，每次只发 dataChanged(单行)，**绝不** beginResetModel —— 流式整表重置会丢滚动位置与 delegate。

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVector>

class MessageListModel : public QAbstractListModel
{
	Q_OBJECT
		Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // "user" | "agent" | "system"
		TextRole,                      // 用户/系统消息的纯文本
		SegmentsRole,                  // 有序段（QVariantList）
		StreamingRole
	};

	enum Kind
	{
		User = 0,
		Agent = 1,
		System = 2
	};

	explicit MessageListModel(QObject* parent = nullptr);

	// 会话切换：整表清空（这里必须重置，因为换了会话）
	void clear();

	int appendUser(const QString& text);
	int appendSystem(const QString& text);
	// 建一条空 Agent 消息（流式会往里填内容）；返回行号
	int appendAgent();

	// 流式/历史共用的追加入口：type = "thinking" | "reply" | "toolCall" | "toolResult"
	void appendChunk(int row, const QString& type, const QString& text, const QString& toolName = QString());
	// 工具结果落到该行最后一个 toolCall 段上（旧壳语义：一次调用的结果就贴在那张卡片里）；没有就新开 toolResult 段
	void appendToolResult(int row, const QString& text);
	void setStreaming(int row, bool streaming);

	// 最后一行是不是 Agent（历史回放时"工具事件接在上一气泡后面"要靠它）
	int lastAgentRow() const;
	int lastRow() const { return m_rows.size() - 1; }
	bool isEmpty() const { return m_rows.isEmpty(); }

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

signals:
	void countChanged();

private:
	struct Segment
	{
		QString type;
		QString text;
		QString toolName;

		// reply 段的 markdown 切分（MarkdownSegmenter::split）**很贵**，而 data(SegmentsRole) 会被 QML 反复取
		// （委托创建 / dataChanged 失效 / 布局重排都读）—— 原先每次在 segmentsFor() 里现算，实测 88 条历史回复
		// 把 GUI 线程卡了 ~7.8 秒（2026-10-03：174 条 records 单次注入），这里算一次就缓存住。
		// mutable：segmentsFor()/data() 都是 const 却要回填缓存；⚠️ 改 text 必须把 partsReady 置回 false。
		mutable QVariantList parts;
		mutable bool partsReady = false;
	};

	struct Row
	{
		int kind = Agent;
		QString text;
		QVector<Segment> segments;
		QString toolResult;          // 最近的工具结果，附在 toolCall 段上显示
		bool streaming = false;
	};

	// 三个 append* 的公共实现：插一行（kind 决定行类型，text 可空）并返回行号
	int appendRow(int kind, const QString& text);

	// 段的 QML 视图（含 reply 的代码围栏切分结果）按需重建，避免每次 data() 都重算
	QVariantList segmentsFor(const Row& row) const;
	void touchRow(int row);

	QVector<Row> m_rows;
};
