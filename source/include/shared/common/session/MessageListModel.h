#pragma once

// 会话消息列表的 QML 模型：一行 = 一条消息（用户 / Agent / 系统提示）。
// Agent 消息内部是**有序段**（thinking / reply / toolCall / toolResult），交错顺序不会丢；reply 段按代码围栏切成单元。
// 流式期间最终 reply 不逐个 chunk 改 segments，而是写进 Row::replyTail（ReplyTailRole）；回合收尾时再落回
// 稳定 reply 段（SegmentsRole + ReplyTailRole 一起发）。这样过程组的段/委托在正文增长时完全不重建。
// 更新策略：段是追加式的，每次只发 dataChanged(单行)，**绝不** beginResetModel —— 整表重置会丢滚动位置与 delegate。

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
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
		ImagesRole,                    // 该行携带的图片附件 [{id, mediaType}]（仅用户行）
		SegmentsRole,                  // 有序段（QVariantList）
		ReplyTailRole,                 // 仅流式期间最终 reply 的完整尾文本；稳定段不变
		ReplyTailPartsRole,            // 流式尾部按 Markdown part 切分，代码块可实时渲染
		StreamingRole,
		DeliverablesRole,              // Agent 回合的交付文件 [{ path, name, suffix, description }]
		UsageRole,                     // Agent 回合累计用量 { inputTokens, outputTokens, ... }
		DurationTextRole               // 回合耗时（"12.4s"）；流式期间为空，finish 时写入
	};

	enum Kind
	{
		User = 0,
		Agent = 1,
		System = 2
	};

	explicit MessageListModel(QObject* parent = nullptr);

	// 会话切换：整表清空（换了会话，必须重置）
	void clear();

	// 收束窗口：删掉最前面的 count 行（内存/委托上限用）。
	// 批量重建期间不触发 insert/remove 信号，直接改内部数组，最后由 endBatchRebuild 统一 reset。
	void removeFirstRows(int count);

	// 批量重建：只在 begin/end 之间发一次 reset，避免历史重建时逐条 insert/dataChanged
	// 把 QML 的 ListView 拖死（加载更多一页 1500+ 条时尤其明显）。
	void beginBatchRebuild();
	void endBatchRebuild();

	// 图片附件引用（用户消息）
	struct ImageRef
	{
		QString attachmentId;
		QString mediaType;
		QString previewUrl;   // 发送瞬间用本地预览；历史消息为空，走 session/attachment
	};

	// `present` 工具产生的交付文件（DSH Web 的 deliverables/presented）。
	struct DeliverableRef
	{
		QString path;
		QString description;
	};
	int appendUser(const QString& text);
	int appendUserWithImages(const QString& text, const QVector<ImageRef>& images);
	// 最后一行是否就是这条用户消息（历史重建时去重用，避免同一 prompt 插两次）
	bool lastRowIsUser(const QString& text, const QVector<ImageRef>& images) const;
	// 历史重建后确认某条用户消息是否已经能从记录里画出来；没有就由 ChatController
	// 把本地刚发送的那条补回末尾，避免“开始输出后用户消息被吞”。
	bool containsUser(const QString& text) const;
	int appendSystem(const QString& text);
	// 建一条空 Agent 消息（流式往里填）；返回行号
	int appendAgent();

	// 流式/历史共用的追加入口：type = "thinking" | "reply" | "toolCall" | "toolResult"
	void appendChunk(int row, const QString& type, const QString& text, const QString& toolName = QString());
	// assistant/message 的 thinking 是权威快照：替换最后一段 thinking，而不是追加。
	void replaceThinkingSnapshot(int row, const QString& text);
	// 工具结果贴到该行最后一个 toolCall 段上（一次调用的结果就在那张卡片里）；没有就新开 toolResult 段
	void appendIntentResult(int row, const QString& text);
	void setStreaming(int row, bool streaming);
	void appendDeliverables(int row, const QVector<DeliverableRef>& files);
	// 累加一轮里多个模型步骤的 usage。字段缺失时按 0 处理。
	void addUsage(int row, const QJsonObject& usage);
	// 回合结束写入耗时文本（"12.4s"）；空串表示未知，QML 据此隐藏。
	void setDurationText(int row, const QString& text);

	// 最后一行是不是 Agent（历史回放时"工具事件接在上一气泡后面"要靠它）
	int lastAgentRow() const;
	int lastRow() const { return m_rows.size() - 1; }
	bool isEmpty() const { return m_rows.isEmpty(); }

	// 换肤后调用：代码块颜色在切分时烘进 parts（CodeHighlighter 按当前主题解析角色），主题一变单元就得重建
	// —— 清缓存 + 整表发一次 dataChanged（段是追加式的，不能 reset）。
	void invalidateSegmentParts();

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
		// （委托创建 / dataChanged 失效 / 布局重排都读）—— 算一次缓存住，别在 segmentsFor 里现算。
		// mutable：segmentsFor/data 都是 const 却要回填缓存；⚠️ 改 text 必须把 partsReady 置回 false。
		mutable QVariantList parts;
		mutable bool partsReady = false;
	};

	struct Row
	{
		int kind = Agent;
		QString text;
		QVector<Segment> segments;
		QVector<ImageRef> images;   // 仅用户行
		QVector<DeliverableRef> deliverables;   // 仅 Agent 行
		QJsonObject usage;          // 仅 Agent 行：本轮累计 token 用量
		QString durationText;        // 仅 Agent 行：整轮耗时，turn 结束后写入
		QString replyTail;           // 仅流式期间：当前最终 reply 的完整尾文本；收尾时落回稳定段
		// 流式尾部 part 缓存：避免 data() 被 QML 反复读取时每次重切全文。
		mutable QVariantList tailParts;
		mutable bool tailPartsReady = false;
		bool streaming = false;
	};

	// 三个 append* 的公共实现：插一行（kind 决定行类型，text 可空）并返回行号
	int appendRow(int kind, const QString& text);

	// 段的 QML 视图（含 reply 的代码围栏切分结果）按需重建，避免每次 data 都重算
	QVariantList segmentsFor(const Row& row) const;
	void touchRow(int row);
	// 流式 reply 的增量尾：只有最终 reply 走这里；必要时补一个空 reply 段占位，保持真实段顺序。
	void appendReplyTail(int row, const QString& text);
	// 把尾文本落回最近的稳定 reply 段（没有就补一段），并清空 replyTail；返回是否真的提交了尾文本。
	bool commitReplyTail(int row, bool notify = true);

	QVector<Row> m_rows;
	bool m_batchRebuild = false;
};
