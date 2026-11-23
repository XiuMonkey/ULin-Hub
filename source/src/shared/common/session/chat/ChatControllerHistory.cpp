#include "common/util/Debugger.h"
#include "common/util/Format.h"
#include "common/util/Json.h"
#include "common/session/EventRecord.h"
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"
#include "common/agent/AgentSessionClient.h"
#include "common/session/TraceModel.h"

#include "common/agent/dsh/DshApiClient.h"
#include "common/agent/AgentEventParser.h"
#include "common/util/ConnectionManager.h"
#include "network/SessionCommands.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include "common/session/chat/ChatControllerInternal.h"

using namespace chatctl;

void ChatController::onFollowSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore)
{
	if (sessionId != m_sessionId) {
		qWarning().noquote() << "[Chat] follow snapshot ignored:"
			<< "snapshotSession=" << sessionId << "currentSession=" << m_sessionId
			<< "records=" << records.size();
		return;   // 迟到的帧：只画当前会话
	}

	// 首帧已经到达（即使内容为空）⇒ 不再是“加载中”，QML 可以决定显示空状态还是消息。
	if (m_sessionLoading) {
		m_sessionLoading = false;
		m_sessionLoadingTimer->stop();
		emit sessionLoadingChanged();
	}

	// ---- 两条守卫：挡掉"重建风暴"（每帧重建整棵 QML 委托会打满一核并让窗口"未响应"）。
	// 空快照来自 `session/page` 的应答（没东西可给就回空），换会话的清空由 setSession() 负责
	// ⇒ 当前会话上的空 records 一律当噪声丢掉，不能拿它把消息区打空。
	if (records.isEmpty() && !m_historyRecords.isEmpty())
		return;

	// 去重：records 完全一样就不重建（cursor/hasMore 单独更新即可）。
	// 原来把 cursor/hasMore 也放进相等条件，所以同一条 1237 条记录连来两帧时仍会整表重建两遍，
	// 在长会话里就是两次几十秒的 QML 委托重建。重建只取决于 records，翻页状态变化只影响顶部按钮。
	// 必须排在 flushPending() 之前 —— 重复帧不该引发任何副作用。
	if (records == m_historyRecords) {
		const bool stateChanged = (cursor != m_cursor) || (hasMore != m_hasOlder);
		m_cursor = cursor;
		m_hasOlder = hasMore;
		if (stateChanged)
			emit olderChanged();
		return;
	}

	flushPending();
	// 首次装载只保留最近 60 行；更早的仍在 m_historyRecords 里，靠「加载更多」翻。
	// 同会话后续快照不再收，避免把已经翻出来的旧页裁掉。
	const bool firstSnapshot = m_historyRecords.isEmpty();
	m_cursor = cursor;  // 记住翻页状态：throughSeq(cursor) / hasMore / 原始记录（旧页拼它前面）
	m_hasOlder = hasMore;
	m_historyRecords = records;
	emit olderChanged();

	// 轨迹视图吃的是同一份账本：快照换了它就跟着重建（两条守卫已经挡掉空帧与重复帧）
	m_trace->setRecords(records);

	rebuildFromRecords(records, firstSnapshot);
}

// 翻页：与 DshApiClient 里 follow 的兜底路径同路（session/page + beforeSeq），
// 记录同样是 {type:"event", event:{…}}，用 SessionCommands::legacyRecords 解包。
void ChatController::loadOlder()
{
	if (!m_api || m_loadingOlder || !m_hasOlder || m_sessionId.isEmpty())
		return;

	int beforeSeq = 0;  // beforeSeq = 当前内容里最早一条的 seq（会话内 seq 单调递增）
	for (const QJsonValue& value : m_historyRecords) {
		const int seq = unwrapRecord(value).value(QStringLiteral("seq")).toInt();
		if (seq > 0) {
			beforeSeq = seq;
			break;
		}
	}
	if (beforeSeq <= 0)
		return;

	m_loadingOlder = true;
	emit olderChanged();

	// 上翻一页按“消息”计，实际会展开成多条事件；200 条一次重建太重，
	// 改成 60 条，翻页次数多了但每次 UI 重建小很多。
	m_agentSession->pageSession(m_sessionId, m_cursor, 60, beforeSeq,
		[this](const QJsonObject& page) {
			m_loadingOlder = false;

			const QJsonArray older = SessionCommands::legacyRecords(page.value(QStringLiteral("records")).toArray());
			if (older.isEmpty()) {
				if (m_rowsTrimmed) {
					// 服务端没有更早的记录了，但本地记录里还藏着被 60 行收束裁掉的行：
					// 重新按全量记录 rebuild 一次，把它们展开，否则这部分历史永远翻不出来。
					m_hasOlder = false;
					emit olderPrepended();
					rebuildFromRecords(m_historyRecords, false);
					emit olderChanged();
					qInfo().noquote() << "[Chat] 加载更多：服务端无更早记录，展开本地收束的行 records="
						<< m_historyRecords.size();
					return;
				}
				m_hasOlder = false;
				emit olderChanged();
				return;
			}

			// 服务端分页在边界上可能回带已经存在的记录；按 seq 去重后再前置。
			// 不去重的话 rebuildFromRecords 会把同一条消息画两遍，而且越上翻重复越多。
			QSet<int> seenSeqs;
			for (const QJsonValue& value : m_historyRecords) {
				const int seq = recordSeq(value);
				if (seq > 0)
					seenSeqs.insert(seq);
			}
			QJsonArray merged;  // 旧页拼到头部（服务端按 seq 升序给，直接前置即可）
			for (const QJsonValue& value : older) {
				const int seq = recordSeq(value);
				if (seq > 0 && seenSeqs.contains(seq))
					continue;
				if (seq > 0)
					seenSeqs.insert(seq);
				merged.append(value);
			}
			for (const QJsonValue& value : m_historyRecords)
				merged.append(value);
			m_historyRecords = merged;
			m_hasOlder = page.value(QStringLiteral("hasMore")).toBool();
			// 同样把整份账本交给轨迹视图（旧页已拼到头部，行序不变）
			m_trace->prependRecords(older);

			qInfo().noquote() << "[Chat] 上翻一页 records=" << older.size() << "hasMore=" << m_hasOlder;

			emit olderPrepended();  // 先通知 QML 记下"插入前的内容高"，再重建（重建会换掉所有行）
			// 上翻是用户主动动作：不要把刚拉到的旧页立刻按 60 行收束裁掉。
			rebuildFromRecords(m_historyRecords, false);
			emit olderChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_loadingOlder = false;
			qWarning().noquote() << "[Chat] session/page 失败:" << error.code << error.message;
			emit olderChanged();
		});
}

// 收束到 kRowKeep：丢掉模型最前面的行（连同它们的段/Markdown 缓存一起释放）。
// 同时按行→seq 映射裁掉这些行对应的 m_historyRecords（只保留本地 session 文件里的原始记录，
// 加载更多时再用 session/page 拉回来）；TraceModel 不参与这次内存收束。
int ChatController::trimRowsToKeep()
{
	if (!m_messages)
		return 0;
	const int count = m_messages->rowCount();
	if (count <= kRowTrigger)
		return 0;
	const int remove = count - kRowKeep;
	if (remove <= 0)
		return 0;

	// 有些提示行由错误/取消路径直接 append，没有经过 onMuxFrame/rebuild 的 seq 记录；
	// 在这里补齐/裁齐，保证 m_rowSeqs 与模型行一一对应（补的用 0 = 无服务端记录）。
	while (m_rowSeqs.size() < count)
		m_rowSeqs.append(0);
	if (m_rowSeqs.size() > count)
		m_rowSeqs.resize(count);

	m_messages->removeFirstRows(remove);
	m_rowsTrimmed = true;
	// 同步裁剪行→seq 映射，并据此把“被收掉的行”对应的原始 records 也从内存里卸掉。
	// 注意：只裁 ChatController 的 m_historyRecords；TraceModel 保留全量（轨迹不参与这次内存收束）。
	int cutSeq = 0;
	if (m_rowSeqs.size() > remove) {
		for (int i = remove; i < m_rowSeqs.size(); ++i) {
			if (m_rowSeqs.at(i) > 0) {
				cutSeq = m_rowSeqs.at(i);
				break;
			}
		}
	}
	if (remove < m_rowSeqs.size())
		m_rowSeqs.remove(0, remove);
	else
		m_rowSeqs.clear();
	if (cutSeq > 0) {
		QJsonArray kept;
		for (const QJsonValue& value : m_historyRecords) {
			const int seq = recordSeq(value);
			if (seq <= 0 || seq >= cutSeq)   // 无 seq 的本地记录保留；有 seq 的裁掉被收束行之前的
				kept.append(value);
		}
		if (kept.size() != m_historyRecords.size()) {
			m_historyRecords = kept;
			m_rowsTrimmed = false;   // 原始记录已经不在了，不能再靠本地 rebuild 把旧行翻回来
		}
	}
	if (m_turnRow >= 0) {
		if (m_turnRow < remove)
			m_turnRow = -1;
		else
			m_turnRow -= remove;
	}
	if (m_pendingDeliverablesRow >= 0) {
		if (m_pendingDeliverablesRow < remove)
			m_pendingDeliverablesRow = -1;
		else
			m_pendingDeliverablesRow -= remove;
	}
	return remove;
}

void ChatController::enforceRowCap()
{
	const int removed = trimRowsToKeep();
	if (removed <= 0)
		return;

	if (!m_hasOlder) {
		m_hasOlder = true;   // 被收掉的行靠顶部「加载更多」重新拉
		emit olderChanged();
	}
	emit messagesChanged();
	qInfo().noquote() << "[Chat] 行数收束" << (removed + kRowKeep) << "->" << kRowKeep
		<< "removed=" << removed << "session=" << m_sessionId;
}

void ChatController::rebuildFromRecords(const QJsonArray& records, bool enforceCap)
{
	m_messages->beginBatchRebuild();
	// 从头重建会把记录里所有行重新画出来，先清"有隐藏行"的标记；若本次仍触发收束，trimRowsToKeep 会重新置 true。
	m_rowsTrimmed = false;
	m_rowSeqs.clear();
	m_turnRow = -1;   // 整表重建：行号全失效，本轮归属也要重来

	QStringList types;  // 诊断用：首帧快照里有哪些事件类型（"空白"是没消息还是解析漏了，一眼可辨）
	QSet<int> seenSeqs;
	// 账本里最后一个回合边界是否还开着（只有 turn/start、还没到 turn/end）：
	// 切走又切回时快照重放的是"仍在进行"的那一轮，必须据此恢复流式态。
	bool turnOpen = false;
	bool turnHadOutput = false;
	int lastRowSeq = 0;
	for (const QJsonValue& value : records) {
		const int seq = recordSeq(value);
		if (seq > 0) {
			if (seenSeqs.contains(seq))
				continue;   // 重复记录（分页/重连快照重叠）只画一次
			seenSeqs.insert(seq);
			lastRowSeq = seq;
		}
		const QJsonObject event = unwrapRecord(value);
		const QString type = event.value(QStringLiteral("type")).toString();
		types << type;
		if (type == QStringLiteral("turn/start")) {
			turnOpen = true;
			turnHadOutput = false;
		} else if (type == QStringLiteral("turn/end")) {
			turnOpen = false;
		} else if (turnOpen
			&& (type == QStringLiteral("assistant/message")
				|| type == QStringLiteral("tool/call")
				|| type == QStringLiteral("tool/result"))) {
			turnHadOutput = true;   // 历史回放只有整段 assistant/message，没有 chunk 增量
		}
		const int before = m_messages->rowCount();
		applyHistoryEvent(event);
		const int added = m_messages->rowCount() - before;
		for (int i = 0; i < added; ++i)
			m_rowSeqs.append(lastRowSeq);
	}
	// 批量重建期间直接收束（此时还没有 reset 信号，QML 最终只看到 <=60 行，避免一次建 77 条重气泡）。
	if (enforceCap)
		trimRowsToKeep();
	m_messages->endBatchRebuild();

	// 服务端记录里还没有这条刚发的用户消息（压缩/快照时序）时，别让它从界面上消失。
	if (!m_pendingUserText.trimmed().isEmpty()) {
		if (m_messages->containsUser(m_pendingUserText)) {
			m_pendingUserText.clear();
			m_pendingUserImages.clear();
		} else {
			m_messages->appendUserWithImages(m_pendingUserText, m_pendingUserImages);
			m_rowSeqs.append(0);   // 本地补回的消息没有服务端 seq
		}
	}
	// pending 用户消息可能让行数回到 70：补一次非批量的收束。
	if (enforceCap)
		enforceRowCap();

	qInfo().noquote() << "[Chat] follow snapshot records=" << records.size()
		<< "types=" << types.join(QLatin1Char(','))
		<< "rows=" << m_messages->rowCount()
		<< "session=" << m_sessionId
		<< "turnOpen=" << turnOpen;

	// 账本显示最后一个回合还没到 turn/end ⇒ 这轮仍在跑。setSession() 已把 m_turnActive / m_streaming
	// 清零；不按账本恢复的话，m_turnActive=false 会挡住 setStreaming(true) 的守卫 —— 停止键永远停在
	// "发送"、气泡显示"已完成"，而服务端仍在继续吐输出。
	const bool wasStreaming = m_streaming;   // 同会话流式中途刷新时保留实况值，别被账本"还没提交的整段"降级
	if (turnOpen && !m_turnActive) {
		m_turnActive = true;
		m_turnHadOutput = turnHadOutput;
	}
	if (turnOpen)
		m_turnWatchdog->start();   // 恢复后继续受看门狗约束（静默过久仍会本地收尾）
	m_streaming = turnOpen && (turnHadOutput || wasStreaming);
	// 行级 streaming 也要补上：气泡靠它决定"整轮折叠行显不显示/摘要"，不写就会在还在输出时显示"已完成"。
	if (m_streaming) {
		const int row = (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
			? m_turnRow : m_messages->lastAgentRow();
		if (row >= 0)
			m_messages->setStreaming(row, true);
	}
	emit streamingChanged();
	emit messagesChanged();
}

void ChatController::applyHistoryEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("user/message")) {
		if (isHiddenContextUserMessage(event))
			return;
		if (isModelSelectionNotice(event)) {   // "[model changed: ...]"：系统通知，别画成用户气泡
			const QString notice = extractEventText(event).trimmed();
			if (!notice.isEmpty())
				m_messages->appendSystem(notice);
			return;
		}
		const QString text = extractEventText(event).trimmed();
		const QVector<MessageListModel::ImageRef> images = extractEventImages(event);
		if (!text.isEmpty() || !images.isEmpty()) {
			// 会话快照里同一 prompt 偶尔会出现两条 user/message（重连/队列重放）；
			// 历史重建时只保留连续相同的一条，避免用户消息上屏两次。
			if (!m_messages->lastRowIsUser(text, images))
				m_messages->appendUserWithImages(text, images);
			m_turnRow = -1;   // 用户开口 ⇒ 上一轮到此为止（turn/start 缺失时的兜底）
		}
		return;
	}

	if (type == QStringLiteral("turn/start") || type == QStringLiteral("turn/end")) {
		// 回合边界：下一个 assistant/message 开新气泡（一轮的典型事件序：turn/start → user → steps → turn/end）
		if (type == QStringLiteral("turn/end")) {
			const QJsonObject usage = event.value(QStringLiteral("data")).toObject()
				.value(QStringLiteral("usage")).toObject();
			if (!usage.isEmpty()) {
				const int row = (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
					? m_turnRow : m_messages->lastAgentRow();
				if (row >= 0)
					m_messages->addUsage(row, usage);
			}
		}
		if (type == QStringLiteral("turn/start"))
			m_turnRow = -1;
		// turn/end 保留行号：历史里偶尔会在 turn/end 后补最后一条
		// assistant/message；下一轮 turn/start / user 会重置。
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		// 历史回放：一轮（turn）合成一个气泡。引擎在一轮里每次工具调用前后都会各发一条
		// assistant/message，同一 turn 的多条续写到 m_turnRow 指向的那一行，段顺序不变。
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		const QJsonObject usage = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("usage")).toObject();
		if (thinking.isEmpty() && reply.isEmpty() && usage.isEmpty())
			return;

		const int row = agentRowForTurn();
		if (!usage.isEmpty())
			m_messages->addUsage(row, usage);
		if (!thinking.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("thinking"), thinking);
		if (!reply.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("reply"), reply);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const IntentCallInfo tool = extractIntentCall(event);
		if (tool.valid)
			m_messages->appendChunk(agentRowForTurn(), QStringLiteral("toolCall"),
				jsonIndented(tool.arguments), tool.name);
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const IntentResultInfo result = extractIntentResult(event);
		if (result.valid)
			m_messages->appendIntentResult(agentRowForTurn(), result.message);
		return;
	}

	if (type == QStringLiteral("deliverables/presented")) {
		applyDeliverablesEvent(event, false);
		return;
	}

	// 斜杠命令结果不进模型历史：引擎只在会话日志里追加 command/run + command/done，这里渲染成系统提示行。
	if (type == QStringLiteral("command/done")) {
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		const QString text = data.value(QStringLiteral("text")).toString();
		const bool error = data.value(QStringLiteral("kind")).toString() == QLatin1String("error");
		if (!text.isEmpty())
			m_messages->appendSystem(error ? qtTrId("chat_command_failed_fmt").arg(text) : text);
		return;
	}
}

void ChatController::applyProjections(int asOfSeq, const QJsonObject& values)
{
	// higher-seq-wins：seq 更小的帧丢弃
	if (asOfSeq < m_projectionSeq)
		return;
	m_projectionSeq = asOfSeq;

	const QJsonObject stats = values.value(QStringLiteral("sessionStats")).toObject();
	if (!stats.isEmpty()) {
		m_usage.turns = stats.value(QStringLiteral("turns")).toInt(m_usage.turns);
		m_usage.steps = stats.value(QStringLiteral("steps")).toInt(m_usage.steps);
		m_usage.llmMs = static_cast<qint64>(stats.value(QStringLiteral("llmMs")).toDouble(m_usage.llmMs));
		m_usage.toolMs = static_cast<qint64>(stats.value(QStringLiteral("toolMs")).toDouble(m_usage.toolMs));
		m_usage.ttftMs = static_cast<qint64>(stats.value(QStringLiteral("ttftMs")).toDouble(m_usage.ttftMs));
		m_usage.ttftSteps = stats.value(QStringLiteral("ttftSteps")).toInt(m_usage.ttftSteps);
		m_usage.decodeMs = static_cast<qint64>(stats.value(QStringLiteral("decodeMs")).toDouble(m_usage.decodeMs));
		m_usage.decodeTokens = static_cast<qint64>(stats.value(QStringLiteral("decodeTokens")).toDouble(m_usage.decodeTokens));
	}

	const QJsonObject usage = values.value(QStringLiteral("tokenUsage")).toObject();
	if (!usage.isEmpty()) {
		m_usage.uncachedInputTokens = static_cast<qint64>(usage.value(QStringLiteral("uncachedInputTokens")).toDouble(0));
		m_usage.cacheReadTokens = static_cast<qint64>(usage.value(QStringLiteral("cacheReadTokens")).toDouble(0));
		m_usage.cacheWriteTokens = static_cast<qint64>(usage.value(QStringLiteral("cacheWriteTokens")).toDouble(0));
		m_usage.outputTokens = static_cast<qint64>(usage.value(QStringLiteral("outputTokens")).toDouble(0));
	}

	// 组间 " | "，组内 " · "
	const qint64 billedInput = m_usage.uncachedInputTokens + m_usage.cacheReadTokens + m_usage.cacheWriteTokens;

	QStringList groups;
	groups << qtTrId("chat_turn_step_fmt").arg(m_usage.turns).arg(m_usage.steps);

	QStringList durations;
	if (m_usage.llmMs > 0)
		durations << qtTrId("chat_llm_label_fmt").arg(formatDuration(m_usage.llmMs));
	if (m_usage.toolMs > 0)
		durations << qtTrId("chat_tool_call_count_fmt").arg(formatDuration(m_usage.toolMs));
	if (!durations.isEmpty())
		groups << durations.join(QStringLiteral(" · "));

	QStringList speeds;
	if (m_usage.ttftSteps > 0) {
		const qint64 average = m_usage.ttftMs / m_usage.ttftSteps;
		speeds << qtTrId("chat_first_token_avg_fmt").arg(formatDuration(average));
	}
	if (m_usage.decodeMs > 0) {
		const double rate = static_cast<double>(m_usage.decodeTokens) / (static_cast<double>(m_usage.decodeMs) / 1000.0);
		speeds << qtTrId("chat_tok_per_sec_fmt").arg(formatThroughput(rate));
	}
	if (!speeds.isEmpty())
		groups << speeds.join(QStringLiteral(" · "));

	if (billedInput > 0) {
		const QString percent = formatCacheHitPercent(billedInput, m_usage.cacheReadTokens);
		if (!percent.isEmpty())
			groups << qtTrId("chat_cache_hit_fmt").arg(percent);
	}

	groups << qtTrId("chat_token_usage_fmt").arg(formatTokenCount(billedInput)).arg(formatTokenCount(m_usage.outputTokens));

	const QString line = groups.join(QStringLiteral(" | "));
	if (m_statsLine == line)
		return;

	m_statsLine = line;
	emit statsLineChanged();
}
