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

void ChatController::onMuxFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString frameType = payload.value(QStringLiteral("type")).toString();

	// 交互面板帧走单独处理器，不进消息区
	if (frameType == QStringLiteral("question/requested")) {
		handleQuestionFrame(frame);
		return;
	}
	if (frameType == QStringLiteral("approval/requested")) {
		handleApprovalFrame(frame);
		return;
	}
	if (frameType != QStringLiteral("session/event"))
		return;   // 其余帧类型不归消息区管

	const QString frameSessionId = payload.value(QStringLiteral("sessionId")).toString();
	if (!frameSessionId.isEmpty() && frameSessionId != m_sessionId)
		return;   // 只渲染当前会话，避免 A 会话的输出串到 B 会话

	const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
	// 只有"回合在干活"的事件才给看门狗续期：生命周期/状态/投影类事件会不时推来，
	// 拿它们当存活信号会让看门狗永远不响。
	if (m_turnActive && isTurnProgressEvent(event.value(QStringLiteral("type")).toString()))
		m_turnWatchdog->start();

	// 轨迹视图：账本事件逐条追加（assistant/chunk 这类增量分片由 TraceModel 自己筛掉）
	m_trace->appendEvent(event);

	const int eventSeq = event.value(QStringLiteral("seq")).toInt();
	const int rowsBefore = m_messages ? m_messages->rowCount() : 0;
	applyStreamEvent(event);
	// 维护行→record seq 映射，供用户发消息时的收束/内存卸载决定裁掉哪些记录。
	const int added = (m_messages ? m_messages->rowCount() : 0) - rowsBefore;
	for (int i = 0; i < added; ++i)
		m_rowSeqs.append(eventSeq > 0 ? eventSeq : m_cursor);
	// 收束只在用户发消息时做一次；流式 chunk / turn 事件不再触发行数判断。
}

void ChatController::beginTurn()
{
	m_turnRow = -1;   // 新一轮 = 新气泡（上一轮那行不再被续写）
	m_pendingDeliverables.clear();
	m_pendingDeliverablesRow = -1;
	m_turnHadOutput = false;
	m_turnActive = true;
	clearCancelPending();   // 新一轮开始 ⇒ 上一轮的中止状态作废（否则按钮一直停在"停止中"）
	m_turnWatchdog->start();
}

void ChatController::applyStreamEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("turn/start")) {
		// 新的一轮：下一条内容开新气泡，上一轮那行不再被续写。
		m_turnRow = -1;
		// 回合可能已被看门狗/中止在本地提前收尾，而服务端又开了一轮（取消没生效、或工具调用后接续）：
		// 必须重新激活，否则这一轮的输出点不亮按钮（setStreaming 的守卫会挡掉）也永远不会收尾。
		if (!m_turnActive)
			beginTurn();
		return;
	}

	if (type == QStringLiteral("turn/end")) {
		// 回合真正的收尾点。先把 turn/end 自带的 usage 归到当前气泡。
		// 刻意不在这里清 m_turnRow：服务端有时在 turn/end 之后才补最后一条
		// assistant/message 快照；清掉后再来会新建一个气泡，整段回复重复。
		// 下一轮 turn/start / user/message / beginTurn 会重置它。
		qInfo().noquote() << "[ChatRecv] turn/end"
			<< "seq=" << event.value(QStringLiteral("seq")).toInt()
			<< "streaming=" << m_streaming
			<< "turnRow=" << m_turnRow
			<< "pending=" << m_pending.size();
		const QJsonObject usage = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("usage")).toObject();
		if (!usage.isEmpty()) {
			const int row = (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
				? m_turnRow : m_messages->lastAgentRow();
			if (row >= 0)
				m_messages->addUsage(row, usage);
		}
		flushPending();
		// 无条件关流式态：哪怕这一轮早已被本地判死（残留态），turn/end 也是官方给的"可以收手了"信号。
		setStreaming(false);
		if (m_turnActive)
			finishTurn();
		else
			clearCancelPending();   // 中止请求到此闭环（活跃态那条由 finishTurn 负责）
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		// 收尾：已流式显示过就不重复追加，只把最后一段封存
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		const int seq = event.value(QStringLiteral("seq")).toInt();
		qInfo().noquote() << "[ChatRecv] assistant/message"
			<< "seq=" << seq
			<< "streaming=" << m_streaming
			<< "turnRow=" << m_turnRow
			<< "thinking=" << thinking.size() << textDigest(thinking)
			<< "reply=" << reply.size() << textDigest(reply)
			<< "pending=" << m_pending.size();
		const QJsonObject usage = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("usage")).toObject();
		// 先把流式残余结算掉：否则整段快照会被 bufferChunk 并进同一条 Pending，
		// appendChunk 收到的是 "残余+全文"，与已画出的正文不满足相等/前缀/累积关系，
		// MessageListModel 的去重失效，正文会被当成增量再追加一遍。
		flushPending();
		if (!thinking.isEmpty() || !reply.isEmpty() || !usage.isEmpty()) {
			// 用 agentRowForTurn()（本轮那一行）：一轮里会来多条 assistant/message
			//（每次工具调用前后各一条），它们属于同一个气泡。
			const int row = agentRowForTurn();
			if (!usage.isEmpty())
				m_messages->addUsage(row, usage);
			if (!thinking.isEmpty())
				m_messages->replaceThinkingSnapshot(row, thinking);
			bufferChunk(row, QStringLiteral("reply"), reply);
			m_turnHadOutput = true;
		}
		flushPending();
		// 这里刻意不 setStreaming(false) + finishTurn()：一条 assistant/message 只代表"这一段说完了"，
		// 一轮里还有下一次工具调用与后续回复；回合结束统一由 turn/end（或看门狗）负责。
		return;
	}

	if (type == QStringLiteral("assistant/chunk")) {
		const QString chunk = extractEventText(event);
		const QString chunkType = extractChunkType(event);
		if (chunk.isEmpty())
			return;
		if (chatTraceEnabled())
			qInfo().noquote() << "[ChatRecv] assistant/chunk"
			<< "seq=" << event.value(QStringLiteral("seq")).toInt()
			<< "type=" << chunkType
			<< "len=" << chunk.size() << textDigest(chunk)
			<< "streaming=" << m_streaming
			<< "turnRow=" << m_turnRow
			<< "pending=" << m_pending.size();

		// chunk.type 缺失时按正文处理；已知类型之外的显式类型才丢弃
		const bool thinking = chunkType == QStringLiteral("reasoning-delta");
		if (!chunkType.isEmpty() && !thinking && chunkType != QStringLiteral("text-delta"))
			return;

		bufferChunk(agentRowForTurn(), thinking ? QStringLiteral("thinking") : QStringLiteral("reply"), chunk);
		m_turnHadOutput = true;   // 流式 chunk 也是可见输出；不标记会被 finishTurn() 误判成"本轮没有收到回复"
		setStreaming(true);
		return;
	}

	if (type == QStringLiteral("text-chunks") || type == QStringLiteral("reasoning-chunks")) {
		const QJsonArray texts = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("texts")).toArray();
		const bool thinking = type == QStringLiteral("reasoning-chunks");

		const int row = agentRowForTurn();
		bool appended = false;
		for (const QJsonValue& value : texts) {
			const QString chunk = value.toString();
			if (chunk.isEmpty())
				continue;
			bufferChunk(row, thinking ? QStringLiteral("thinking") : QStringLiteral("reply"), chunk);
			appended = true;
		}
		if (appended)
			setStreaming(true);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const IntentCallInfo tool = extractIntentCall(event);
		if (tool.valid) {
			bufferChunk(agentRowForTurn(), QStringLiteral("toolCall"),
				jsonIndented(tool.arguments), tool.name);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const IntentResultInfo result = extractIntentResult(event);
		if (result.valid) {
			flushPending();  // 工具结果是整段落的，直接落盘（并先把缓冲刷掉，保证顺序）
			m_messages->appendIntentResult(agentRowForTurn(), result.message);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	if (type == QStringLiteral("deliverables/presented")) {
		applyDeliverablesEvent(event, true);
		return;
	}

	// 斜杠命令的结果（新追加的那条）：实时渲染成系统提示行，理由同 applyHistoryEvent
	if (type == QStringLiteral("command/done")) {
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		const QString text = data.value(QStringLiteral("text")).toString();
		const bool error = data.value(QStringLiteral("kind")).toString() == QLatin1String("error");
		if (!text.isEmpty()) {
			flushPending();   // 系统行是整段落的，先落盘保证顺序
			m_messages->appendSystem(error ? qtTrId("chat_command_failed_fmt").arg(text) : text);
		}
		return;
	}

	// 命令注册表变了（插件运行期注册/注销命令）⇒ 让宿主重拉一次命令表：这是通知不是事件，只翻一个 signal。
	if (type == QStringLiteral("commands/change")) {
		emit commandRegistryChanged();
		return;
	}

	// 后端会话异常：当前客户端收不到这条负载（走 DshApiClient 的 `$events` type=="emit" 分支，
	// 那里只记事件名），真正兜底的是上面的回合看门狗；这条分支留着防御。
	if (type == QStringLiteral("api-session/error")) {
		qWarning().noquote() << "[Chat] backend session error:" << event.value(QStringLiteral("data"));
		flushPending();
		setStreaming(false);
		finishTurn();
		return;
	}

	// 实时流里的用户态消息：自己发的那条发送时已上屏（见 sendPrompt），这里只关心子代理回传/结束通知
	// —— 它们不画，但到达就意味着运行态变了。
	if (type == QStringLiteral("user/message")) {
		if (isHiddenContextUserMessage(event))
			return;
		// 服务端回显了本地消息 ⇒ 不再需要重建补回。
		m_pendingUserText.clear();
		m_pendingUserImages.clear();
		if (isPeerRelayUserMessage(event)) {
			emit peerActivitySeen();
			return;
		}
		if (isModelSelectionNotice(event)) {   // "[model changed: ...]"：系统通知，别画成用户气泡
			const QString notice = extractEventText(event).trimmed();
			if (!notice.isEmpty()) {
				flushPending();
				m_messages->appendSystem(notice);
			}
			return;
		}
		return;
	}

	// 子代理"有事发生"：目录新增、回传给父会话、结束通知 —— 都不改消息区，但要通知上层
	// 轻量重拉一次 session.list：运行态（running）只有那条 RPC 会给，投影里没有。
	if (type == QStringLiteral("subagent/catalog")
		|| type == QStringLiteral("agent/inbox/spliced")) {
		emit peerActivitySeen();
		return;
	}

	// 其余事件类型不参与渲染。每类只记一次（commands/change、投影类事件量大，逐帧打日志会淹掉排障通道）
	if (!m_loggedEventTypes.contains(type)) {
		m_loggedEventTypes.insert(type);
		qInfo().noquote() << "[Chat] 忽略的事件类型（每类只记一次）:" << type;
	}
}

void ChatController::finishTurn()
{
	m_turnActive = false;
	if (!m_pendingDeliverables.isEmpty() && m_messages) {
		const int row = (m_pendingDeliverablesRow >= 0
			&& m_pendingDeliverablesRow < m_messages->rowCount())
			? m_pendingDeliverablesRow : m_messages->lastAgentRow();
		if (row >= 0)
			m_messages->appendDeliverables(row, m_pendingDeliverables);
	}
	m_pendingDeliverables.clear();
	m_pendingDeliverablesRow = -1;
	m_turnWatchdog->stop();
	clearCancelPending();   // 回合收尾 ⇒ 中止请求到此闭环（无论有没有点过停止）
	m_lastTurnHadOutput = m_turnHadOutput;

	// 回合收尾仍无可见输出时补一条系统提示
	if (!m_turnHadOutput) {
		m_messages->appendSystem(qtTrId("chat_empty_reply_hint"));
		qInfo().noquote() << "[Chat] turn finished without visible output -> hint shown";
	}

	m_turnHadOutput = false;
	emit turnFinished();
}

int ChatController::agentRowForTurn()
{
	if (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
		return m_turnRow;

	m_turnRow = m_messages->appendAgent();
	return m_turnRow;
}

// 流式分片先按 (行, 类型, 工具名) 合并进 m_pending，由 flush 定时器或收尾动作统一刷给模型。
void ChatController::bufferChunk(int row, const QString& type, const QString& text, const QString& toolName)
{
	if (row < 0 || text.isEmpty())
		return;

	if (!m_pending.isEmpty()) {
		Pending& last = m_pending.last();
		if (last.row == row && last.type == type && last.toolName == toolName) {
			last.text += text;
			if (!m_flushTimer->isActive())
				m_flushTimer->start();
			return;
		}
	}

	Pending entry;
	entry.row = row;
	entry.type = type;
	entry.text = text;
	entry.toolName = toolName;
	m_pending.append(entry);

	if (!m_flushTimer->isActive())
		m_flushTimer->start();
}

void ChatController::flushPending()
{
	m_flushTimer->stop();
	if (m_pending.isEmpty())
		return;

	const QVector<Pending> pending = m_pending;
	m_pending.clear();
	if (chatTraceEnabled())
		qInfo().noquote() << "[ChatFlush] entries=" << pending.size();
	// 同一批里可能按顺序含 reply / thinking / toolCall；模型按到达顺序落段，缓冲不能重排它们。
	for (const Pending& entry : pending) {
		if (chatTraceEnabled())
			qInfo().noquote() << "[ChatFlush]"
			<< "row=" << entry.row
			<< "type=" << entry.type
			<< "len=" << entry.text.size() << textDigest(entry.text)
			<< "tool=" << entry.toolName;
		m_messages->appendChunk(entry.row, entry.type, entry.text, entry.toolName);
	}
}

void ChatController::setStreaming(bool streaming)
{
	// 不变量：回合不活跃时不允许点亮"停止"按钮。看门狗把回合判死之后，服务端迟到的 chunk
	// 仍会走到 setStreaming(true)，按钮又变回"停止"；而 turn/end 与看门狗续期都以 m_turnActive
	// 为前置条件 ⇒ 再没有任何路径能关回 false。迟到的内容照常追加，只是不改"这一轮还活着"的判断。
	if (streaming && !m_turnActive)
		return;

	if (m_streaming == streaming)
		return;

	m_streaming = streaming;
	// 同时把行级标志写上：气泡靠它决定"整轮折叠行显不显示 / 摘要取首行还是最近完成段落"，
	// 不写就会在还在输出时显示"已完成"。
	if (m_messages) {
		// 优先"本轮那一行"（一轮一个气泡）；没有就退回最后一条 agent 行
		const int row = (m_turnRow >= 0 && m_turnRow < m_messages->rowCount())
			? m_turnRow
			: m_messages->lastAgentRow();
		if (row >= 0)
			m_messages->setStreaming(row, streaming);
	}
	emit streamingChanged();
}

// 清掉"正在中止"：turn/end 到达、中止看门狗超时、RPC 失败、换会话时都走这里。
// 单独成函数是为了不漏 emit —— 漏了会让 QML 的按钮永久停在灰态。
void ChatController::clearCancelPending()
{
	m_cancelWatchdog->stop();
	if (!m_cancelPending)
		return;

	m_cancelPending = false;
	m_cancelSentAtMs = 0;
	emit cancellingChanged();
}
