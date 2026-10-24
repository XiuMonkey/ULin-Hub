#pragma once

// 轨迹视图的数据面（对照官方 @deepseek-ai/dsh-client-ui-trajectory）。
// 官方那套是"纯投影"：不额外问服务端要数据，而是把会话事件账本（turn/step/消息/工具/压缩）
// 摊成一张按轮次组织的记录表。这里做同一件事 —— 数据来源就是 ChatController 已经在收的那条流：
//  · session/follow 首帧快照的 records（历史）
//  · mux 上的 session/event 帧（流式）
// 所以**没有新增任何 RPC**，也不需要服务端配合。
// 与官方的差距（有意为之，见 build 说明）：
//  · 不做 14 个标签页的检查器，只给一份"原文 JSON + 时序 + 用量"的检查面板；
//  · 不做可拖拽时间轴（Overview），只保留每条记录的耗时与 TTFT 文本；
//  · 不做虚拟化（ListView 自己复用委托，行数上千也没有压力）。
// 事件信封（dsh-session 的 SessionEvent）：{ type, seq, time(ms), data }。
// 本模型只吃"账本事件"，**忽略 assistant/chunk** —— 那是 LLM 增量分片，不是账本记录，
// 官方的记录表同样不显示（它只显示 assistant/message 这条已结算的记录）。

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

class TraceModel : public QAbstractListModel
{
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
	// 顶部概览：turns / steps / tools / tokens / durationMs / errors
	Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // turn|step|user|assistant|tool|context|system|compaction|attempt|retry
		SeqRole,
		TimeTextRole,        // "12:04:51"
		TitleRole,           // 主标题：角色名 / 工具名 / "第 3 轮"
		SubtitleRole,        // 摘要：正文首行 / 参数或结果预览
		DetailRole,          // 检查面板的原文（格式化 JSON）
		DurationTextRole,    // "1.2s"（未知时为空）
		TokensTextRole,      // "in 1.2k · out 340"
		StatusKindRole,      // "" | ok | error | running | warn
		StatusTextRole,      // "error · tool_not_found" / "进行中"
		DepthRole,           // 0 轮 / 1 步 / 2 记录
		LiveRole             // 该行是否仍处于进行中
	};

	explicit TraceModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	QVariantMap summary() const { return m_summary; }

	// 换会话：整表清空
	void clear();

	// 快照 / 上翻一页：整表重建（records 是 [{type:"event", event:{…}}]，与 ChatController 同形）
	void setRecords(const QJsonArray& records);

	// 流式：追加一条账本事件。非账本事件（assistant/chunk 等）直接丢掉。
	void appendEvent(const QJsonObject& event);

	// 暂停投影（对应官方"向上滚动就停跟随，新记录不打断对旧记录的检查"）。
	// 暂停期间事件照收（进 m_events），只是**不投影成行**；恢复时补一次增量投影。
	void setPaused(bool paused);
	bool paused() const { return m_paused; }

signals:
	void countChanged();
	void summaryChanged();

private:
	struct Row
	{
		QString kind;
		QString timeText;
		QString title;
		QString subtitle;
		QString detail;
		QString durationText;
		QString tokensText;
		QString statusKind;
		QString statusText;
		int seq = 0;
		int depth = 2;
		bool live = false;
	};

	void rebuild();
	// 一条事件能变成 0..n 行（返回时新行先落在 m_pendingRows 里）
	void projectEvent(const QJsonObject& event);
	void recomputeSummary();

	// 清投影状态（跨事件保留的那堆"当前位置"与配对表），rebuild 前用
	void resetProjectionState();
	// 投影 m_events[fromEvent, end)：新行进 m_pendingRows，被回填的旧行记进 m_touchedRows
	void projectRange(int fromEvent);
	// 增量投影：把还没投影的事件补上（恢复暂停 / 流式追加都走这里）
	void projectPending();
	// 把 m_pendingRows 里的新行以 beginInsertRows 的形式并进 m_rows，并通知被回填的行
	void flushPendingRows();
	// 行指针（新行还在 pending 区时也能拿到）；越界返回 nullptr
	Row* rowAt(int rowIndex);
	// 旧行被回填时登记一下，投影结束后只对这些行发 dataChanged
	void markTouched(int rowIndex);

	QVector<QJsonObject> m_events;   // 原始账本（已按 seq 去重、升序）
	QVector<Row> m_rows;
	QVector<Row> m_pendingRows;      // 本次投影新产生的行（尚未进 m_rows）
	QSet<int> m_touchedRows;         // 本次投影回填过的旧行号
	QVariantMap m_summary;
	bool m_paused = false;           // 暂停期间只收事件、不投影成行
	int m_projectedCount = 0;        // 已投影到 m_events 的哪一条（增量投影的游标）

	// 投影过程中的"当前位置"（跨事件保留，rebuild 前重置）
	int m_currentTurnRow = -1;
	int m_currentStepRow = -1;
	int m_currentTurn = 0;
	int m_currentStep = 0;
	qint64 m_stepStartMs = 0;
	qint64 m_turnStartMs = 0;

	// tool/call 与 tool/result 是两条事件，靠 toolCallId 配对：
	// 行号用于把结果回填到已建的那一行；事件本体与起始时刻留着拼检查面板的 detail 和算耗时。
	QHash<QString, int> m_callRows;
	QHash<QString, QJsonObject> m_callEvents;
	QHash<QString, qint64> m_callTimes;

	// 汇总计数
	int m_turns = 0;
	int m_steps = 0;
	int m_tools = 0;
	int m_errors = 0;
	qint64 m_inputTokens = 0;
	qint64 m_outputTokens = 0;
	qint64 m_firstTimeMs = 0;
	qint64 m_lastTimeMs = 0;
};
