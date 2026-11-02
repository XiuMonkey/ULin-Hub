#pragma once

// 会话消息列表的 QML 模型：一行 = 一条消息（用户 / Agent / 系统提示）。
// Agent 消息内部是**有序段**（thinking / reply / toolCall / toolResult），交错顺序不会丢；reply 段按代码围栏切成单元。
// 更新策略：段是追加式的，每次只发 dataChanged(单行)，**绝不** beginResetModel —— 整表重置会丢滚动位置与 delegate。

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
		ImagesRole,                    // 该行携带的图片附件 [{id, mediaType}]（仅用户行）
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

	// 会话切换：整表清空（换了会话，必须重置）
	void clear();

	// 图片附件引用（用户消息）
	struct ImageRef
	{
		QString attachmentId;
		QString mediaType;
		QString previewUrl;   // 发送瞬间用本地预览；历史消息为空，走 session/attachment
	};
	int appendUser(const QString& text);
	int appendUserWithImages(const QString& text, const QVector<ImageRef>& images);
	// 连续重复的历史用户消息（引擎快照里可能同一个 prompt 出现两次）不重复插入
	bool lastRowIsUser(const QString& text, const QVector<ImageRef>& images) const;
	int appendSystem(const QString& text);
	// 建一条空 Agent 消息（流式往里填）；返回行号
	int appendAgent();

	// 流式/历史共用的追加入口：type = "thinking" | "reply" | "toolCall" | "toolResult"
	void appendChunk(int row, const QString& type, const QString& text, const QString& toolName = QString());
	// 工具结果贴到该行最后一个 toolCall 段上（一次调用的结果就在那张卡片里）；没有就新开 toolResult 段
	void appendToolResult(int row, const QString& text);
	void setStreaming(int row, bool streaming);

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
		QString toolResult;          // 最近的工具结果，附在 toolCall 段上显示
		bool streaming = false;
	};

	// 三个 append* 的公共实现：插一行（kind 决定行类型，text 可空）并返回行号
	int appendRow(int kind, const QString& text);

	// 段的 QML 视图（含 reply 的代码围栏切分结果）按需重建，避免每次 data 都重算
	QVariantList segmentsFor(const Row& row) const;
	void touchRow(int row);

	QVector<Row> m_rows;
};
