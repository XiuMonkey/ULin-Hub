// 见 include/common/session/TraceModel.h：会话事件账本 -> 轨迹记录表。
// 这里刻意**不碰 UI**：所有文案（"第 N 轮"/"工具"/"进行中"…）都由 QML 用 qsTrId 拼，
// 模型只吐数据（kind + 数字 + 原文）。这样加语言不用改 C++，也不会出现"模型里塞中文"的老问题。

#include "common/session/TraceModel.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

namespace
{
	QJsonObject unwrapRecord(const QJsonValue& value)
	{
		QJsonObject event = value.toObject();
		if (event.value(QStringLiteral("event")).isObject())
			event = event.value(QStringLiteral("event")).toObject();
		return event;
	}

	qint64 eventTime(const QJsonObject& event)
	{
		return static_cast<qint64>(event.value(QStringLiteral("time")).toDouble());
	}

	QString blocksText(const QJsonValue& content)   // 内容块数组 -> 纯文本（只取 text 块；image / tool 等留给检查面板看原文）
	{
		if (content.isString())
			return content.toString();
		if (!content.isArray())
			return QString();

		QStringList parts;
		const QJsonArray blocks = content.toArray();
		for (const QJsonValue& blockValue : blocks) {
			const QJsonObject block = blockValue.toObject();
			if (block.value(QStringLiteral("type")).toString() == QStringLiteral("text")
				|| block.contains(QStringLiteral("text"))) {
				const QString text = block.value(QStringLiteral("text")).toString();
				if (!text.isEmpty())
					parts << text;
			}
		}
		return parts.join(QLatin1Char('\n'));
	}

	QString messageText(const QJsonObject& message)
	{
		QString text = blocksText(message.value(QStringLiteral("content")));
		if (text.isEmpty())
			text = message.value(QStringLiteral("text")).toString();
		return text;
	}

	// 只抽 reasoning 块。很多步骤（尤其纯工具调用那几步）的正文里一个 text 块都没有，
	// 只有推理，这时记录行拿推理当预览才有信息量。
	QString reasoningText(const QJsonObject& message)
	{
		const QJsonArray blocks = message.value(QStringLiteral("content")).toArray();
		QStringList parts;
		for (const QJsonValue& blockValue : blocks) {
			const QJsonObject block = blockValue.toObject();
			if (block.value(QStringLiteral("type")).toString() != QStringLiteral("reasoning"))
				continue;
			const QString text = block.value(QStringLiteral("text")).toString();
			if (!text.isEmpty())
				parts << text;
		}
		return parts.join(QLatin1Char('\n'));
	}

	QString preview(const QString& text, int limit = 400)   // 单行预览：换行折成空格，超长截断（委托里还有 elide，这里只为不让模型带着整篇正文）
	{
		const QString flat = text.simplified();
		if (flat.size() > limit)
			return flat.left(limit) + QStringLiteral("…");
		return flat;
	}

	QString elapsed(qint64 ms)
	{
		if (ms <= 0)
			return QString();
		if (ms < 1000)
			return QString::number(ms) + QStringLiteral("ms");
		if (ms < 60000)
			return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral("s");
		return QString::number(ms / 60000) + QStringLiteral("m")
			+ QString::number((ms % 60000) / 1000) + QStringLiteral("s");
	}

	QString compactCount(qint64 value)
	{
		if (value <= 0)
			return QString();
		if (value < 1000)
			return QString::number(value);
		if (value < 1000000)
			return QString::number(value / 1000.0, 'f', 1) + QStringLiteral("k");
		return QString::number(value / 1000000.0, 'f', 1) + QStringLiteral("M");
	}

	QString prettyJson(const QJsonObject& object)
	{
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}

	QString timeText(qint64 ms)
	{
		if (ms <= 0)
			return QString();
		return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss"));
	}

	// 账本事件：只有这些才进记录表。assistant/chunk 是 LLM 增量分片（不是持久事件），
	// 官方记录表同样不显示；request/header 与 tool/ptc-dispatch 在这里也只占一次重建、
	// 却不产生任何一行 —— 所以一并排除（真要查它们得走 original 事件导出，不在本视图范围）。
	bool isLedgerEvent(const QString& type)
	{
		static const QSet<QString> ledger = {
			QStringLiteral("turn/start"), QStringLiteral("turn/end"),
			QStringLiteral("step/start"), QStringLiteral("step/end"),
			QStringLiteral("user/message"), QStringLiteral("assistant/message"),
			QStringLiteral("assistant/attempt"), QStringLiteral("developer/message"),
			QStringLiteral("system/message"), QStringLiteral("tool/call"),
			QStringLiteral("tool/result"), QStringLiteral("compaction/start"),
			QStringLiteral("compaction/end"), QStringLiteral("compaction/summary"),
			QStringLiteral("llm/retry")
		};
		return ledger.contains(type);
	}

	// 一条 assistant/message 的流式记录：最早的 token 时刻、最晚的 token 时刻。
	// AssistantStreamRecord = { type, time0, index, dt[], texts[] }，dt 是相邻分片的时间差。
	void streamSpan(const QJsonArray& stream, qint64* firstTokenMs, qint64* lastTokenMs)
	{
		for (const QJsonValue& recordValue : stream) {
			const QJsonObject record = recordValue.toObject();
			const qint64 time0 = static_cast<qint64>(record.value(QStringLiteral("time0")).toDouble());
			if (time0 <= 0)
				continue;

			const QJsonArray dt = record.value(QStringLiteral("dt")).toArray();
			if (!dt.isEmpty()) {
				const qint64 first = time0 + static_cast<qint64>(dt.first().toDouble());
				if (*firstTokenMs == 0 || first < *firstTokenMs)
					*firstTokenMs = first;
			}

			qint64 end = time0;
			for (const QJsonValue& delta : dt)
				end += static_cast<qint64>(delta.toDouble());
			if (end > *lastTokenMs)
				*lastTokenMs = end;
		}
	}
}

TraceModel::TraceModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

int TraceModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant TraceModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();

	const Row& row = m_rows.at(index.row());
	switch (role) {
	case KindRole: return row.kind;
	case SeqRole: return row.seq;
	case TimeTextRole: return row.timeText;
	case TitleRole: return row.title;
	case SubtitleRole: return row.subtitle;
	case DetailRole: return row.detail;
	case DurationTextRole: return row.durationText;
	case TokensTextRole: return row.tokensText;
	case StatusKindRole: return row.statusKind;
	case StatusTextRole: return row.statusText;
	case DepthRole: return row.depth;
	case LiveRole: return row.live;
	default: return QVariant();
	}
}

QHash<int, QByteArray> TraceModel::roleNames() const
{
	return {
		{ KindRole, "kind" },
		{ SeqRole, "seq" },
		{ TimeTextRole, "timeText" },
		{ TitleRole, "title" },
		{ SubtitleRole, "subtitle" },
		{ DetailRole, "detail" },
		{ DurationTextRole, "durationText" },
		{ TokensTextRole, "tokensText" },
		{ StatusKindRole, "statusKind" },
		{ StatusTextRole, "statusText" },
		{ DepthRole, "depth" },
		{ LiveRole, "live" }
	};
}

void TraceModel::clear()
{
	m_events.clear();
	m_rows.clear();
	m_pendingRows.clear();
	m_touchedRows.clear();
	m_summary.clear();
	m_projectedCount = 0;
	m_paused = false;   // 换会话时解除暂停：否则新会话的事件会一直攒着不显示
	resetProjectionState();
	beginResetModel();
	endResetModel();
	emit countChanged();
	emit summaryChanged();
}

void TraceModel::setRecords(const QJsonArray& records)
{
	QVector<QJsonObject> events;
	events.reserve(records.size());

	int lastSeq = -1;
	for (const QJsonValue& value : records) {
		const QJsonObject event = unwrapRecord(value);
		if (event.isEmpty())
			continue;
		const int seq = event.value(QStringLiteral("seq")).toInt();
		if (seq > 0 && seq <= lastSeq)
			continue;   // 服务端按 seq 升序给；真遇到重复/倒退就跳过，免得同一行出现两遍
		if (seq > 0)
			lastSeq = seq;
		events.append(event);
	}

	if (events == m_events)
		return;   // 快照重放（同一份账本又推一遍）：不重建，省掉一次滚动位置丢失

	m_events = events;
	rebuild();
}

void TraceModel::appendEvent(const QJsonObject& event)
{
	if (event.isEmpty())
		return;
	if (!isLedgerEvent(event.value(QStringLiteral("type")).toString()))
		return;

	const int seq = event.value(QStringLiteral("seq")).toInt();
	if (seq > 0 && !m_events.isEmpty()) {
		const int lastSeq = m_events.last().value(QStringLiteral("seq")).toInt();
		if (seq <= lastSeq)
			return;   // 重放/重复帧
	}

	m_events.append(event);

	if (m_paused)   // 暂停中：只记账不投影（用户在往回翻，表不该继续长、行号也不该继续变）
		return;

	// 增量投影：只把新事件变成行，用 beginInsertRows 追加 —— 不 reset，
	// 否则 ListView 每次都要重建全部委托，滚动位置和滚动条长度会跟着抖。
	projectPending();
}

void TraceModel::setPaused(bool paused)
{
	if (m_paused == paused)
		return;
	m_paused = paused;
	if (!m_paused)   // 恢复时把暂停期间攒下的事件一次性补投影（走增量路径，已有行的位置不动）
		projectPending();
}

// 整表重建：换会话、follow 首帧快照、上翻一页。
// ⚠️ 只有这里允许 beginResetModel —— 流式追加绝不能走 reset：
// ListView 在 reset 时把全部委托作废重排，行高不一时 contentHeight（估值）会来回修正
void TraceModel::rebuild()
{
	beginResetModel();

	m_rows.clear();
	m_rows.reserve(m_events.size() + 8);
	resetProjectionState();
	projectRange(0);
	for (const Row& row : m_pendingRows)
		m_rows.append(row);
	m_pendingRows.clear();
	m_touchedRows.clear();
	m_projectedCount = m_events.size();

	endResetModel();
	recomputeSummary();
	emit countChanged();
}

// 增量投影：新行用 beginInsertRows 追加，被回填的旧行用 dataChanged 点对点通知。
// 这样 ListView 的委托与滚动位置都保留，只有"表尾长了一截"。
void TraceModel::projectPending()
{
	if (m_projectedCount >= m_events.size())
		return;

	projectRange(m_projectedCount);
	m_projectedCount = m_events.size();
	flushPendingRows();

	recomputeSummary();
	emit countChanged();
}

void TraceModel::projectRange(int fromEvent)
{
	m_pendingRows.clear();
	m_touchedRows.clear();

	const int begin = qMax(0, fromEvent);
	for (int i = begin; i < m_events.size(); ++i)
		projectEvent(m_events.at(i));
}

void TraceModel::flushPendingRows()
{
	const int first = m_rows.size();
	const int added = m_pendingRows.size();

	if (added > 0) {
		beginInsertRows(QModelIndex(), first, first + added - 1);
		m_rows.reserve(first + added);
		for (const Row& row : m_pendingRows)
			m_rows.append(row);
		m_pendingRows.clear();
		endInsertRows();
	}

	// 被回填的旧行（轮次/步骤结束时的耗时、工具结果）——只报真正动过的那些。
	// 插进表里的新行不用报：它们插入时内容已经是最终的。
	for (int rowIndex : m_touchedRows) {
		if (rowIndex >= 0 && rowIndex < first)
			emit dataChanged(index(rowIndex), index(rowIndex));
	}
	m_touchedRows.clear();
}

TraceModel::Row* TraceModel::rowAt(int rowIndex)
{
	if (rowIndex < 0)
		return nullptr;
	if (rowIndex < m_rows.size())
		return &m_rows[rowIndex];

	const int pendingIndex = rowIndex - m_rows.size();
	if (pendingIndex < m_pendingRows.size())
		return &m_pendingRows[pendingIndex];
	return nullptr;
}

void TraceModel::markTouched(int rowIndex)
{
	if (rowIndex >= 0 && rowIndex < m_rows.size())
		m_touchedRows.insert(rowIndex);
}

void TraceModel::resetProjectionState()
{
	m_callRows.clear();
	m_callEvents.clear();
	m_callTimes.clear();
	m_currentTurnRow = -1;
	m_currentStepRow = -1;
	m_currentTurn = 0;
	m_currentStep = 0;
	m_stepStartMs = 0;
	m_turnStartMs = 0;
	m_turns = 0;
	m_steps = 0;
	m_tools = 0;
	m_errors = 0;
	m_inputTokens = 0;
	m_outputTokens = 0;
	m_firstTimeMs = 0;
	m_lastTimeMs = 0;
}

void TraceModel::projectEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();
	const QJsonObject data = event.value(QStringLiteral("data")).toObject();
	const int seq = event.value(QStringLiteral("seq")).toInt();
	const qint64 timeMs = eventTime(event);

	if (timeMs > 0) {
		if (m_firstTimeMs == 0 || timeMs < m_firstTimeMs)
			m_firstTimeMs = timeMs;
		if (timeMs > m_lastTimeMs)
			m_lastTimeMs = timeMs;
	}

	// 新行先进 pending 区，本批投影结束后由 flushPendingRows() 统一 beginInsertRows 并进 m_rows。
	// 行号按"m_rows + pending"的最终位置算，所以 m_currentTurnRow 之类照旧可以存/取。
	const auto appendRow = [this](Row row) -> int {
		m_pendingRows.append(row);
		return m_rows.size() + m_pendingRows.size() - 1;
	};

	if (type == QStringLiteral("turn/start")) {
		m_currentTurn = data.value(QStringLiteral("turn")).toInt();
		m_currentStep = 0;
		m_turnStartMs = timeMs;
		++m_turns;

		Row row;
		row.kind = QStringLiteral("turn");
		row.title = QString::number(m_currentTurn);
		row.timeText = timeText(timeMs);
		row.statusKind = QStringLiteral("running");
		row.live = true;
		row.depth = 0;
		row.seq = seq;
		row.detail = prettyJson(event);
		m_currentTurnRow = appendRow(row);
		return;
	}

	if (type == QStringLiteral("turn/end")) {
		if (Row* row = rowAt(m_currentTurnRow)) {
			markTouched(m_currentTurnRow);
			row->live = false;
			row->statusKind.clear();
			const QJsonValue reasonValue = data.value(QStringLiteral("reason"));   // 早先按字符串取会得到空串，轮次行右侧的状态就永远不显示
			row->statusText = reasonValue.isObject()
				? reasonValue.toObject().value(QStringLiteral("kind")).toString()
				: reasonValue.toString();
			if (timeMs > 0 && m_turnStartMs > 0)
				row->durationText = elapsed(timeMs - m_turnStartMs);
			row->detail = prettyJson(event);
		}
		return;
	}

	if (type == QStringLiteral("step/start")) {
		m_currentStep = data.value(QStringLiteral("step")).toInt();
		m_stepStartMs = timeMs;
		++m_steps;

		Row row;
		row.kind = QStringLiteral("step");
		row.title = QString::number(m_currentTurn) + QLatin1Char('.') + QString::number(m_currentStep);
		row.timeText = timeText(timeMs);
		row.statusKind = QStringLiteral("running");
		row.live = true;
		row.depth = 1;
		row.seq = seq;
		row.detail = prettyJson(event);
		m_currentStepRow = appendRow(row);
		return;
	}

	if (type == QStringLiteral("step/end")) {
		if (Row* row = rowAt(m_currentStepRow)) {
			markTouched(m_currentStepRow);
			row->live = false;
			row->statusKind.clear();
			row->detail = prettyJson(event);
			if (timeMs > 0 && m_stepStartMs > 0)
				row->durationText = elapsed(timeMs - m_stepStartMs);
		}
		return;
	}

	if (type == QStringLiteral("user/message")) {
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;
		const QJsonObject source = message.value(QStringLiteral("source")).toObject();
		const QString form = source.value(QStringLiteral("form")).toString();

		Row row;

		// 隐藏的上下文注入（snapshot/instructions/catalog）不是人发的 —— 标成 context，视觉上弱一档
		row.kind = form.isEmpty() ? QStringLiteral("user") : QStringLiteral("context");
		row.title = form;
		row.subtitle = preview(messageText(message));
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;

		const QJsonObject usage = data.value(QStringLiteral("usage")).toObject();
		const qint64 inputTokens = static_cast<qint64>(usage.value(QStringLiteral("inputTokens")).toDouble());
		const qint64 outputTokens = static_cast<qint64>(usage.value(QStringLiteral("outputTokens")).toDouble());
		m_inputTokens += inputTokens;
		m_outputTokens += outputTokens;

		qint64 firstToken = 0;
		qint64 lastToken = 0;
		streamSpan(data.value(QStringLiteral("stream")).toArray(), &firstToken, &lastToken);

		Row row;
		row.kind = QStringLiteral("assistant");
		QString body = messageText(message);   // 纯工具调用的步骤没有 text 块，退到 reasoning 当预览（否则整行只剩右侧的用量数字）
		if (body.isEmpty())
			body = reasoningText(message);
		row.subtitle = preview(body);
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		if (inputTokens > 0 || outputTokens > 0)
			row.tokensText = QStringLiteral("↑") + compactCount(inputTokens)
				+ QStringLiteral(" ↓") + compactCount(outputTokens);

		if (firstToken > 0 && lastToken > firstToken) {
			const qint64 generation = lastToken - firstToken;   // TTFT 从"这一步开始"量（官方口径）；步骤起点未知时退化成只报生成耗时
			if (m_stepStartMs > 0 && firstToken >= m_stepStartMs) {
				row.durationText = QStringLiteral("TTFT ") + elapsed(firstToken - m_stepStartMs)
					+ QStringLiteral(" · ") + elapsed(generation);
			} else {
				row.durationText = elapsed(generation);
			}
		}
		if (data.value(QStringLiteral("interrupted")).toBool()) {
			row.statusKind = QStringLiteral("warn");
			row.statusText = QStringLiteral("interrupted");
		}
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	if (type == QStringLiteral("assistant/attempt")) {
		qint64 firstToken = 0;
		qint64 lastToken = 0;
		streamSpan(data.value(QStringLiteral("stream")).toArray(), &firstToken, &lastToken);

		Row row;
		row.kind = QStringLiteral("attempt");
		row.title = QString::number(m_currentStep);
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		row.statusKind = QStringLiteral("warn");
		if (firstToken > 0 && lastToken > firstToken)
			row.durationText = elapsed(lastToken - firstToken);
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const QString callId = data.value(QStringLiteral("callId")).toString();
		const QString name = data.value(QStringLiteral("name")).toString();
		const QString arguments = data.value(QStringLiteral("arguments")).toString();

		QString prettyArguments = arguments;   // arguments 是模型产出的**原始 JSON 字符串**（未解析）；能解析就美化，不能就原样
		const QJsonDocument parsed = QJsonDocument::fromJson(arguments.toUtf8());
		if (!parsed.isNull())
			prettyArguments = QString::fromUtf8(parsed.toJson(QJsonDocument::Indented));

		++m_tools;

		Row row;
		row.kind = QStringLiteral("tool");
		row.title = name;
		row.subtitle = preview(prettyArguments);
		row.timeText = timeText(timeMs);
		row.statusKind = QStringLiteral("running");
		row.live = true;
		row.depth = 2;
		row.seq = seq;

		QJsonObject detail;
		detail.insert(QStringLiteral("call"), event);
		row.detail = prettyJson(detail);

		const int rowIndex = appendRow(row);
		if (!callId.isEmpty()) {
			m_callRows.insert(callId, rowIndex);
			m_callEvents.insert(callId, event);
			m_callTimes.insert(callId, timeMs);
		}
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const QJsonObject message = data.value(QStringLiteral("message")).toObject();
		const QString callId = message.value(QStringLiteral("toolCallId")).toString();
		const QJsonObject error = data.value(QStringLiteral("error")).toObject();
		const bool failed = message.value(QStringLiteral("isError")).toBool() || !error.isEmpty();

		QString output = blocksText(message.value(QStringLiteral("content")));
		if (output.isEmpty())
			output = messageText(message);

		const int rowIndex = m_callRows.value(callId, -1);
		if (Row* row = rowAt(rowIndex)) {
			markTouched(rowIndex);
			row->live = false;
			if (!output.isEmpty())   // 结果预览比参数更贴近"这一行发生了什么"，有正文就换掉
				row->subtitle = preview(output);
			if (failed) {
				++m_errors;
				row->statusKind = QStringLiteral("error");
				row->statusText = error.value(QStringLiteral("code")).toString();
				if (row->statusText.isEmpty())
					row->statusText = error.value(QStringLiteral("name")).toString();
			} else {
				row->statusKind = QStringLiteral("ok");
			}

			const qint64 startedAt = m_callTimes.value(callId, 0);   // 耗时只能靠两条事件的 time 差（账本里没有专门计时字段）—— 官方 Inspect 的 Timing 页同口径
			if (startedAt > 0 && timeMs > startedAt)
				row->durationText = elapsed(timeMs - startedAt);

			QJsonObject detail;
			detail.insert(QStringLiteral("call"), m_callEvents.value(callId));
			detail.insert(QStringLiteral("result"), event);
			row->detail = prettyJson(detail);
		} else {
			Row orphan;   // 结果没有对应的 call（历史被截断 / 分页边界）：单独成行，不要吞掉
			orphan.kind = QStringLiteral("tool");
			orphan.title = message.value(QStringLiteral("name")).toString();
			orphan.subtitle = preview(output);
			orphan.timeText = timeText(timeMs);
			orphan.depth = 2;
			orphan.seq = seq;
			orphan.statusKind = failed ? QStringLiteral("error") : QStringLiteral("ok");
			orphan.statusText = error.value(QStringLiteral("code")).toString();
			orphan.detail = prettyJson(event);
			appendRow(orphan);
			if (failed)
				++m_errors;
		}
		return;
	}

	if (type == QStringLiteral("system/message") || type == QStringLiteral("developer/message")) {
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;

		Row row;
		row.kind = QStringLiteral("system");
		row.title = type == QStringLiteral("system/message")
			? QStringLiteral("system") : QStringLiteral("developer");
		row.subtitle = preview(messageText(message));
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	if (type.startsWith(QLatin1String("compaction/"))) {
		Row row;
		row.kind = QStringLiteral("compaction");
		row.title = type.mid(QStringLiteral("compaction/").size());
		if (type == QStringLiteral("compaction/summary"))
			row.subtitle = preview(data.value(QStringLiteral("summary")).toString());
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		row.statusKind = type.endsWith(QLatin1String("/end")) ? QStringLiteral("ok") : QString();
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	if (type == QStringLiteral("llm/retry")) {
		Row row;
		row.kind = QStringLiteral("retry");
		row.subtitle = preview(data.value(QStringLiteral("reason")).toString());
		row.timeText = timeText(timeMs);
		row.depth = 2;
		row.seq = seq;
		row.statusKind = QStringLiteral("warn");
		row.detail = prettyJson(event);
		appendRow(row);
		return;
	}

	// request/header、tool/ptc-dispatch 等：只在检查面板里有意义，不占记录表一行
}

void TraceModel::recomputeSummary()
{
	QVariantMap summary;
	summary.insert(QStringLiteral("turns"), m_turns);
	summary.insert(QStringLiteral("steps"), m_steps);
	summary.insert(QStringLiteral("tools"), m_tools);
	summary.insert(QStringLiteral("errors"), m_errors);
	summary.insert(QStringLiteral("inputTokens"), static_cast<qlonglong>(m_inputTokens));
	summary.insert(QStringLiteral("outputTokens"), static_cast<qlonglong>(m_outputTokens));
	summary.insert(QStringLiteral("durationMs"),
		m_firstTimeMs > 0 && m_lastTimeMs > m_firstTimeMs ? m_lastTimeMs - m_firstTimeMs : 0LL);
	summary.insert(QStringLiteral("durationText"), elapsed(m_lastTimeMs - m_firstTimeMs));
	m_summary = summary;
	emit summaryChanged();
}
