// 见 ChatController.h：事件流 -> 消息模型。
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"

#include "network/DshApiClient.h"
#include "network/DshEventParser.h"

#include "network/SessionCommands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

namespace
{
	constexpr int kStreamFlushMs = 50;
	// 回合看门狗：这么久没有任何本会话事件就认为后端没在干活（典型：没配模型 / 会话 resume 失败）
	constexpr int kTurnWatchdogMs = 15000;

	// 隐藏的上下文注入（运行时快照/系统说明/目录）不是用户真正发的消息 —— 与旧壳同一判定
	bool isHiddenContextUserMessage(const QJsonObject& event)
	{
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;
		const QString form = message.value(QStringLiteral("source")).toObject()
			.value(QStringLiteral("form")).toString();
		return form == QStringLiteral("snapshot")
			|| form == QStringLiteral("instructions")
			|| form == QStringLiteral("catalog");
	}

	// 历史记录可能包一层 { event: {...} }
	QJsonObject unwrapRecord(const QJsonValue& value)
	{
		QJsonObject event = value.toObject();
		if (event.value(QStringLiteral("event")).isObject())
			event = event.value(QStringLiteral("event")).toObject();
		return event;
	}

	QString prettyArguments(const QJsonObject& arguments)
	{
		return QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented));
	}

	// 表示"模型/回合真的在推进"的事件类型（看门狗只认这些续期）。
	// 刻意**不包含**会话生命周期/状态、投影、标题、命令变更等类型 —— 它们会长时间周期性推来。
	bool isTurnProgressEvent(const QString& type)
	{
		return type.startsWith(QLatin1String("assistant/"))
			|| type == QLatin1String("text-chunks")
			|| type == QLatin1String("reasoning-chunks")
			|| type == QLatin1String("tool/call")
			|| type == QLatin1String("tool/result")
			|| type == QLatin1String("step/start")
			|| type == QLatin1String("step/end")
			|| type == QLatin1String("turn/start");
	}

	// ---- 统计行的四个格式化函数（逐条照搬原版 SessionStatsLine 的实现）----

	QString formatDuration(qint64 ms)
	{
		if (ms <= 0)
			return QString();

		if (ms < 60000)
			return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral("s");

		const qint64 seconds = ms / 1000;
		return QStringLiteral("%1m%2s").arg(seconds / 60).arg(seconds % 60);
	}

	QString formatTokens(qint64 value)
	{
		if (value < 1000)
			return QString::number(value);
		if (value < 1000000)
			return QString::number(value / 1000.0, 'f', value >= 100000 ? 0 : 1) + QStringLiteral("K");
		return QString::number(value / 1000000.0, 'f', value >= 100000000 ? 0 : 1) + QStringLiteral("M");
	}

	QString formatThroughput(double value)
	{
		return QString::number(value, 'f', value >= 10.0 ? 0 : 1);
	}

	QString formatCacheHitPercent(qint64 billedInput, qint64 cacheRead)
	{
		if (billedInput <= 0)
			return QString();
		if (cacheRead >= billedInput)
			return QStringLiteral("100");

		const double ratio = static_cast<double>(cacheRead) / static_cast<double>(billedInput) * 100.0;
		const int rounded = qRound(ratio);
		if (rounded < 100)
			return QString::number(rounded);

		// 四舍五入到 100 但并未真的 100：依次给 1–4 位小数，兜底 99.99
		for (int digits = 1; digits <= 4; ++digits) {
			const QString text = QString::number(ratio, 'f', digits);
			if (text != QLatin1String("100") && text != QLatin1String("100.0")
				&& text != QLatin1String("100.00") && text != QLatin1String("100.000")
				&& text != QLatin1String("100.0000")) {
				return text;
			}
		}
		return QStringLiteral("99.99");
	}
}

ChatController::ChatController(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
	, m_messages(new MessageListModel(this))
{
	connect(m_messages, &QAbstractItemModel::modelReset, this, &ChatController::messagesChanged);
	connect(m_messages, &QAbstractItemModel::rowsInserted, this, &ChatController::messagesChanged);

	m_flushTimer = new QTimer(this);
	m_flushTimer->setInterval(kStreamFlushMs);
	connect(m_flushTimer, &QTimer::timeout, this, &ChatController::flushPending);

	m_turnWatchdog = new QTimer(this);
	m_turnWatchdog->setSingleShot(true);
	m_turnWatchdog->setInterval(kTurnWatchdogMs);
	connect(m_turnWatchdog, &QTimer::timeout, this, [this]() {
		if (!m_turnActive)
			return;
		qWarning().noquote() << "[Chat] 回合看门狗超时：本会话" << kTurnWatchdogMs
			<< "ms 内没有任何事件（多半是后端没配模型/会话 resume 失败）";
		flushPending();
		setStreaming(false);
		finishTurn();
	});
}

// ------------------------------------------------------------------
// 发送一轮提示：从 qmlbridge/ShellController.cpp 迁入（2026-10-10 分层）
// 原来这段在 QML 单例里，会让"接线类"同时承担发送策略与自愈状态机 —— 那是业务，不是接线。
// ------------------------------------------------------------------

void ChatController::sendPrompt(const QString& sessionId, const QString& text,
	const std::function<void()>& restartServer,
	const std::function<void(const QString&)>& setStatusLine)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || sessionId.isEmpty() || !m_api)
		return;

	// 会话 id 是入参（当前会话归宿主 qmlbridge），但本类内部的 m_sessionId 也得跟上：
	// 迟到的快照/帧都按它过滤，补发也按它发。正常情况下宿主先 selectSession() 再 sendPrompt()，
	// 这里只是兜底（同一会话时 setSession 自己会早退，不会误清消息区）。
	if (m_sessionId != sessionId)
		setSession(sessionId);

	// 服务端明确没有可用模型时拦下来并说明（原版 MessageHost：chat_no_model_hint）
	// 目录未加载（未知）不拦 —— 与旧壳同一判定
	if (modelCatalogMissing()) {
		if (m_messages)
			m_messages->appendSystem(qtTrId("chat_no_model_hint"));
		return;
	}

	beginTurn();  // 用户消息立即上屏（旧壳同语义：user/message 不重复画；applyStreamEvent 无此分支）
	if (m_messages)
		m_messages->appendUser(trimmed);

	m_api->callMethod(QStringLiteral("session/prompt"),
		SessionCommands::sessionPrompt(sessionId, trimmed),
		[](const QJsonObject&) {},
		[this, trimmed, sessionId, restartServer, setStatusLine](const DshApiClient::RpcError& error) {
			// 与旧壳同一文案（chat_send_failed_fmt）；同时记一行，便于看清服务端拒绝的原因
			qWarning().noquote() << "[Chat] session/prompt failed:" << error.code << error.message;

			// 会话写句柄还挂在**已经退出的旧进程**上（服务端不会因为客户端消失就立刻释放）：
			// 重启内置服务端清掉占用，随后把这条消息补发一次。
			// 旧壳没这条自愈（它只是把失败文案丢进消息区），但这里不修就永远发不出消息。
			const bool busy = error.message.contains(QLatin1String("already owned"))
				|| error.message.contains(QLatin1String("resume failed"));
			if (busy && !m_pendingRetry && restartServer) {
				m_pendingRetry = true;
				m_retryText = trimmed;
				m_retrySessionId = sessionId;
				if (m_messages) {
					m_messages->appendSystem(qtTrId("chat_session_busy_restarting"));
				}
				if (setStatusLine)
					setStatusLine(qtTrId("chat_session_busy_status"));
				restartServer();
				return;
			}

			const QString text = qtTrId("chat_send_failed_fmt").arg(error.code, error.message);
			if (setStatusLine)
				setStatusLine(text);
			if (m_messages)
				m_messages->appendSystem(text);
		});
}

void ChatController::sendPromptAgain()
{
	if (!m_pendingRetry)   // 没攒着待补发的（宿主在每次重连时都会问一下）
		return;

	m_pendingRetry = false;
	const QString text = m_retryText;
	const QString sessionId = m_retrySessionId;
	m_retryText.clear();
	m_retrySessionId.clear();

	// 补发时不重复画用户气泡（首次发送时已经上屏）—— beginTurn 仍要调，重置"本轮有无可见输出"
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty() || sessionId.isEmpty() || !m_api)
		return;

	beginTurn();
	m_api->callMethod(QStringLiteral("session/prompt"),
		SessionCommands::sessionPrompt(sessionId, trimmed),
		[](const QJsonObject&) {},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/prompt (retry) failed:" << error.code << error.message;
			if (m_messages)
				m_messages->appendSystem(qtTrId("chat_send_failed_fmt").arg(error.code, error.message));
		});
}

void ChatController::cancel()
{
	if (m_sessionId.isEmpty() || !m_api)
		return;

	m_api->callMethod(QStringLiteral("session/cancel"), SessionCommands::sessionCancel(m_sessionId),
		[](const QJsonObject&) {},
		[this](const DshApiClient::RpcError& error) {
			// ⚠️ 这里不能直接 setStatus（ChatController 不认识状态行），旧实现在 ShellController 里干这事。
			// 中止失败的概率极低且用户在界面上立刻能看到回合没停，所以只留日志；要露给用户就再加信号。
			qWarning().noquote() << "[Chat] session/cancel failed:" << error.code << error.message;
		});
}

// 换会话：清空消息区并记住当前会话（其它会话的事件一律丢弃）
void ChatController::setSession(const QString& sessionId)
{
	if (m_sessionId == sessionId)
		return;

	flushPending();
	m_sessionId = sessionId;
	m_streaming = false;
	m_turnActive = false;
	m_loggedEventTypes.clear();
	m_messages->clear();
	emit streamingChanged();
	emit messagesChanged();
}

int ChatController::messageCount() const
{
	return m_messages ? m_messages->rowCount() : 0;
}

void ChatController::connectApi()
{
	if (!m_api || m_connected)
		return;
	m_connected = true;

	// follow 首帧快照（session/follow）与 mux 帧：本类的事件入口
	connect(m_api, &DshApiClient::sessionSnapshotReady, this, &ChatController::onFollowSnapshot);
	connect(m_api, &DshApiClient::muxFrameReceived, this, &ChatController::onMuxFrame);

	// 快照里带一份全量投影（含 sessionStats / tokenUsage）——小灰字统计行的数据源。
	// 只认当前会话（迟到的帧直接丢，与 onFollowSnapshot 同一判据）
	connect(m_api, &DshApiClient::sessionProjectionsReady, this,
		[this](const QString& sessionId, int asOfSeq, const QJsonObject& values) {
			if (sessionId == m_sessionId)
				applyProjections(asOfSeq, values);
		});
}
void ChatController::onFollowSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore)
{
	if (sessionId != m_sessionId)
		return;   // 迟到的帧：只画当前会话

	// ---- 两条守卫：挡掉"重建风暴"（2026-10-03 实测：同一毫秒内反复推 records=174/0，每帧都重建整棵
	// QML 委托 ⇒ CPU 一核满载、私有内存每秒涨 ~30MB、窗口报"未响应"）。空快照来自 `session/page` 的应答
	// （DshBridge::sessionSnapshot(..., QJsonArray(), false) 没东西可给就回空），而换会话的清空由
	// setSession() 负责 ⇒ 当前会话上的空 records 一律当噪声丢掉，不能拿它把消息区打空。
	if (records.isEmpty() && !m_historyRecords.isEmpty())
		return;

	// 去重：与上次收到的完全一样就什么都不用做（cursor/hasMore 一起比，免得漏掉翻页状态更新）。
	// ⚠️ 顺序要紧：这条必须排在 flushPending() 之前 —— 重复帧不该引发任何副作用。
	if (records == m_historyRecords && cursor == m_cursor && hasMore == m_hasOlder)
		return;

	flushPending();
	m_cursor = cursor;  // 记住翻页状态：throughSeq(cursor) / hasMore / 原始记录（旧页拼它前面）
	m_hasOlder = hasMore;
	m_historyRecords = records;
	emit olderChanged();

	rebuildFromRecords(records);
}

// M4b 翻页：与 DshApiClient 里 follow 的兜底路径同路（session/page + beforeSeq），
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

	m_api->callMethod(QStringLiteral("session/page"),
		SessionCommands::sessionPage(m_sessionId, m_cursor, 200, beforeSeq),
		[this](const QJsonObject& page) {
			m_loadingOlder = false;

			const QJsonArray older = SessionCommands::legacyRecords(
				page.value(QStringLiteral("records")).toArray());
			if (older.isEmpty()) {
				m_hasOlder = false;
				emit olderChanged();
				return;
			}

			QJsonArray merged;  // 旧页拼到头部（服务端按 seq 升序给，直接前置即可）
			for (const QJsonValue& value : older)
				merged.append(value);
			for (const QJsonValue& value : m_historyRecords)
				merged.append(value);
			m_historyRecords = merged;
			m_hasOlder = page.value(QStringLiteral("hasMore")).toBool();

			qInfo().noquote() << "[Chat] 上翻一页 records=" << older.size() << "hasMore=" << m_hasOlder;

			emit olderPrepended();  // 先通知 QML 记下"插入前的内容高"，再重建（重建会换掉所有行）
			rebuildFromRecords(m_historyRecords);
			emit olderChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_loadingOlder = false;
			qWarning().noquote() << "[Chat] session/page 失败:" << error.code << error.message;
			emit olderChanged();
		});
}

void ChatController::rebuildFromRecords(const QJsonArray& records)
{
	m_messages->clear();

	QStringList types;  // 诊断用：首帧快照里有哪些事件类型（"空白"是没消息还是解析漏了，一眼可辨）
	for (const QJsonValue& value : records) {
		const QJsonObject event = unwrapRecord(value);
		types << event.value(QStringLiteral("type")).toString();
	}
	qInfo().noquote() << "[Chat] follow snapshot records=" << records.size()
		<< "types=" << types.join(QLatin1Char(','));

	for (const QJsonValue& value : records)
		applyHistoryEvent(unwrapRecord(value));

	m_streaming = false;
	emit streamingChanged();
	emit messagesChanged();
}

void ChatController::applyHistoryEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("user/message")) {
		if (isHiddenContextUserMessage(event))
			return;
		const QString text = extractEventText(event).trimmed();
		if (!text.isEmpty())
			m_messages->appendUser(text);
		return;
	}

	if (type == QStringLiteral("assistant/message")) {
		// 历史回放：每条 assistant/message 各成一条气泡（与旧壳 appendHistoryEventsToQuery 一致）
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		if (thinking.isEmpty() && reply.isEmpty())
			return;

		const int row = m_messages->appendAgent();
		if (!thinking.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("thinking"), thinking);
		if (!reply.isEmpty())
			m_messages->appendChunk(row, QStringLiteral("reply"), reply);
		return;
	}

	if (type == QStringLiteral("tool/call")) {
		const ToolCallInfo tool = extractToolCall(event);
		if (tool.valid)
			m_messages->appendChunk(targetAgentRow(), QStringLiteral("toolCall"),
				prettyArguments(tool.arguments), tool.name);
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const ToolResultInfo result = extractToolResult(event);
		if (result.valid)
			m_messages->appendToolResult(targetAgentRow(), result.message);
		return;
	}

	// 斜杠命令的结果。命令**不进模型历史**：引擎只在会话日志里追加 command/run + command/done，
	// 这里把它渲染成一条系统提示行（与官方 Web UI "渲染在模型历史之外"同一语义）。
	if (type == QStringLiteral("command/done")) {
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		const QString text = data.value(QStringLiteral("text")).toString();
		const bool error = data.value(QStringLiteral("kind")).toString() == QLatin1String("error");
		if (!text.isEmpty())
			m_messages->appendSystem(error ? qtTrId("chat_command_failed_fmt").arg(text) : text);
		return;
	}
}

void ChatController::onMuxFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString frameType = payload.value(QStringLiteral("type")).toString();

	// 交互面板（M4b）：与旧壳 MessageHost::handleMuxFrame 同一处路由
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
	// 只有"回合在干活"的事件才给看门狗续期：会话生命周期与状态/投影类事件会不时推来，
	// 拿它们当存活信号会让看门狗永远不响（实测：turn/start…step/end 之后不再有内容，
	// 但状态类事件仍会续期，于是"没输出"的兜底永远不出现）。
	if (m_turnActive && isTurnProgressEvent(event.value(QStringLiteral("type")).toString()))
		m_turnWatchdog->start();

	applyStreamEvent(event);
}

void ChatController::beginTurn()
{
	m_turnHadOutput = false;
	m_turnActive = true;
	m_turnWatchdog->start();
}

// ---- 交互面板（M4b）：与旧壳 InteractionHandler 同样的字段与应答结构 ----

void ChatController::handleQuestionFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QJsonArray questions = payload.value(QStringLiteral("questions")).toArray();

	if (rpcId.isEmpty() || sessionId.isEmpty() || questions.isEmpty()) {
		// 与旧壳一致：帧无效时给一条系统提示，别让用户以为是卡住了
		qWarning().noquote() << "[Chat] question/requested 帧无效，已忽略";
		return;
	}

	QVariantList rows;
	for (const QJsonValue& questionValue : questions) {
		const QJsonObject question = questionValue.toObject();
		QVariantMap row;
		row.insert(QStringLiteral("id"), question.value(QStringLiteral("id")).toString());
		row.insert(QStringLiteral("header"), question.value(QStringLiteral("header")).toString());
		row.insert(QStringLiteral("question"), question.value(QStringLiteral("question")).toString());
		row.insert(QStringLiteral("multiSelect"), question.value(QStringLiteral("multiSelect")).toBool());

		QVariantList options;
		for (const QJsonValue& optionValue : question.value(QStringLiteral("options")).toArray()) {
			const QJsonObject option = optionValue.toObject();
			QVariantMap item;
			item.insert(QStringLiteral("label"), option.value(QStringLiteral("label")).toString());
			item.insert(QStringLiteral("description"), option.value(QStringLiteral("description")).toString());
			options.append(item);
		}
		row.insert(QStringLiteral("options"), options);
		rows.append(row);
	}

	m_questionRpcId = rpcId;
	m_questionSessionId = sessionId;
	m_questions = rows;
	qInfo().noquote() << "[Chat] question/requested 提问数=" << rows.size() << " session=" << sessionId;
	emit interactionChanged();
}

void ChatController::handleApprovalFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QString approvalId = payload.value(QStringLiteral("approvalId")).toString();

	if (rpcId.isEmpty() || sessionId.isEmpty() || approvalId.isEmpty()) {
		qWarning().noquote() << "[Chat] approval/requested 帧无效，已忽略";
		return;
	}

	m_approvalRpcId = rpcId;
	m_approvalSessionId = sessionId;
	m_approvalId = approvalId;
	m_approvalToolName = payload.value(QStringLiteral("toolName")).toString();
	m_approvalReason = payload.value(QStringLiteral("reason")).toString();
	qInfo().noquote() << "[Chat] approval/requested 工具=" << m_approvalToolName;
	emit interactionChanged();
}

void ChatController::submitAnswers(const QVariantList& answers)
{
	if (m_questionRpcId.isEmpty() || !m_api)
		return;

	QJsonArray answerArray;
	for (const QVariant& value : answers) {
		const QVariantMap row = value.toMap();
		QJsonObject answer;
		answer.insert(QStringLiteral("id"), row.value(QStringLiteral("id")).toString());

		QJsonArray selected;
		for (const QVariant& label : row.value(QStringLiteral("selected")).toList())
			selected.append(label.toString());
		answer.insert(QStringLiteral("selected"), selected);

		const QString custom = row.value(QStringLiteral("custom")).toString().trimmed();
		if (!custom.isEmpty())
			answer.insert(QStringLiteral("custom"), custom);

		answerArray.append(answer);
	}

	QJsonObject answer;
	answer.insert(QStringLiteral("answers"), answerArray);

	QJsonObject envelope;  // 与旧壳完全相同的结构：{ sessionId, answer: { answers } }
	envelope.insert(QStringLiteral("sessionId"), m_questionSessionId);
	envelope.insert(QStringLiteral("answer"), answer);

	m_api->respond(m_questionRpcId, envelope);

	const QString rpcId = m_questionRpcId;  // 答完即收起（服务端不会就同一帧再问一次）
	m_questionRpcId.clear();
	m_questionSessionId.clear();
	m_questions.clear();
	qInfo().noquote() << "[Chat] question 已应答 rpcId=" << rpcId;
	emit interactionChanged();
}

void ChatController::respondApproval(bool allow)
{
	if (m_approvalRpcId.isEmpty() || !m_api)
		return;

	QJsonObject answer;
	answer.insert(QStringLiteral("sessionId"), m_approvalSessionId);
	answer.insert(QStringLiteral("approvalId"), m_approvalId);
	answer.insert(QStringLiteral("outcome"),
		allow ? QStringLiteral("allowed-once") : QStringLiteral("rejected"));

	m_api->respond(m_approvalRpcId, answer);

	const QString rpcId = m_approvalRpcId;
	m_approvalRpcId.clear();
	m_approvalSessionId.clear();
	m_approvalId.clear();
	m_approvalToolName.clear();
	m_approvalReason.clear();
	qInfo().noquote() << "[Chat] approval 已应答 rpcId=" << rpcId << " allow=" << allow;
	emit interactionChanged();
}

void ChatController::applyStreamEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("assistant/message")) {
		// 收尾：已流式显示过就不重复追加，只把最后一段封存
		const QString thinking = extractThinking(event);
		const QString reply = extractReply(event);
		if (!thinking.isEmpty() || !reply.isEmpty()) {
			const int row = targetAgentRow();
			bufferChunk(row, QStringLiteral("thinking"), thinking);
			bufferChunk(row, QStringLiteral("reply"), reply);
			m_turnHadOutput = true;
		}
		flushPending();
		setStreaming(false);
		finishTurn();
		return;
	}

	if (type == QStringLiteral("assistant/chunk")) {
		const QString chunk = extractEventText(event);
		const QString chunkType = extractChunkType(event);
		if (chunk.isEmpty())
			return;

		// chunk.type 缺失时按正文处理；已知类型之外的显式类型才丢弃（与旧壳一致）
		const bool thinking = chunkType == QStringLiteral("reasoning-delta");
		if (!chunkType.isEmpty() && !thinking && chunkType != QStringLiteral("text-delta"))
			return;

		bufferChunk(targetAgentRow(), thinking ? QStringLiteral("thinking") : QStringLiteral("reply"), chunk);
		setStreaming(true);
		return;
	}

	if (type == QStringLiteral("text-chunks") || type == QStringLiteral("reasoning-chunks")) {
		const QJsonArray texts = event.value(QStringLiteral("data")).toObject()
			.value(QStringLiteral("texts")).toArray();
		const bool thinking = type == QStringLiteral("reasoning-chunks");

		const int row = targetAgentRow();
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
		const ToolCallInfo tool = extractToolCall(event);
		if (tool.valid) {
			bufferChunk(targetAgentRow(), QStringLiteral("toolCall"),
				prettyArguments(tool.arguments), tool.name);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	if (type == QStringLiteral("tool/result")) {
		const ToolResultInfo result = extractToolResult(event);
		if (result.valid) {
			flushPending();  // 工具结果是整段落的，直接落盘（并先把缓冲刷掉，保证顺序）
			m_messages->appendToolResult(targetAgentRow(), result.message);
			m_turnHadOutput = true;
			setStreaming(true);
		}
		return;
	}

	// 斜杠命令的结果（新追加的那条）：实时渲染成系统提示行，理由同 applyHistoryEvent 里的同款分支
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

	// 命令注册表变了（插件运行期注册/注销命令）⇒ 让宿主重拉一次命令表。
	// 这是**通知**不是事件：不渲染、也不刷屏，只翻一个 signal。
	if (type == QStringLiteral("commands/change")) {
		emit commandRegistryChanged();
		return;
	}

	// 后端会话异常：**当前客户端收不到这条负载**（它走 DshApiClient 的 `$events` type=="emit"
	// 分支，那里只记事件名），所以真正兜底的是上面的回合看门狗。这条分支留着防御：
	// 将来若把 emit 负载转发出来，这里就能直接显示原因。
	if (type == QStringLiteral("api-session/error")) {
		qWarning().noquote() << "[Chat] backend session error:" << event.value(QStringLiteral("data"));
		flushPending();
		setStreaming(false);
		finishTurn();
		return;
	}

	// 其余事件类型不参与渲染。**每类只记一次**（旧壳也会收到大量 commands/change、投影类事件，
	// 逐帧打日志会把排障通道淹掉）
	if (!m_loggedEventTypes.contains(type)) {
		m_loggedEventTypes.insert(type);
		qInfo().noquote() << "[Chat] 忽略的事件类型（每类只记一次）:" << type;
	}
}

void ChatController::finishTurn()
{
	m_turnActive = false;
	m_turnWatchdog->stop();
	m_lastTurnHadOutput = m_turnHadOutput;

	// 与旧壳同一条兜底：回合收尾仍无可见输出时补系统提示
	if (!m_turnHadOutput) {
		m_messages->appendSystem(qtTrId("chat_empty_reply_hint"));
		qInfo().noquote() << "[Chat] turn finished without visible output -> hint shown";
	}

	m_turnHadOutput = false;
	emit turnFinished();
}

int ChatController::targetAgentRow()
{
	const int existing = m_messages->lastAgentRow();
	if (existing >= 0)
		return existing;

	return m_messages->appendAgent();
}

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
	for (const Pending& entry : pending)
		m_messages->appendChunk(entry.row, entry.type, entry.text, entry.toolName);
}

void ChatController::setStreaming(bool streaming)
{
	if (m_streaming == streaming)
		return;

	m_streaming = streaming;
	emit streamingChanged();
}

// ---------------------------------------------------------------------------
// 小灰字统计行：只合并 sessionStats / tokenUsage 两块投影，再按原版分组规则拼一行
// ---------------------------------------------------------------------------

void ChatController::applyProjections(int asOfSeq, const QJsonObject& values)
{
	// higher-seq-wins（与旧壳 SessionProjectionState::merge 同规则）
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

	// 组序与分隔符照搬原版 formatStats：组间 " | "，组内 " · "
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

	groups << qtTrId("chat_token_usage_fmt").arg(formatTokens(billedInput)).arg(formatTokens(m_usage.outputTokens));

	const QString line = groups.join(QStringLiteral(" | "));
	if (m_statsLine == line)
		return;

	m_statsLine = line;
	emit statsLineChanged();
}

// ---------------------------------------------------------------------------
// 模型 / 思考档位（原版 ModelSelector）
// ---------------------------------------------------------------------------

void ChatController::refreshModelCatalog()
{
	if (!m_api || m_sessionId.isEmpty())
		return;

	ModelSelectionService::fetch(m_api, m_sessionId,
		[this](const SessionModelDirectory& directory) {
			m_directory = directory;
			// 会话记录过的选择优先（provider/model 为空 = 服务端无记录，保持目录默认）
			if (!m_overrideProvider.isEmpty() && !m_overrideModel.isEmpty()) {
				if (m_directory.findModel(m_overrideProvider, m_overrideModel)) {
					m_directory.current.provider = m_overrideProvider;
					m_directory.current.model = m_overrideModel;
					if (!m_overrideEffort.isEmpty())
						m_directory.current.reasoningEffort = m_overrideEffort;
				}
				else {
					qInfo().noquote() << "[Chat] 会话记录的模型不在目录里:" << m_overrideProvider << m_overrideModel;
				}
			}
			m_catalogLoaded = true;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			// 失败不拦发送（未知，不是"没有模型"）；只清空目录让 chip 隐藏
			qWarning().noquote() << "[Chat] session/modelCatalog failed:" << error.code << error.message;
			m_directory = SessionModelDirectory();
			m_catalogLoaded = false;
			rebuildCatalog();
		});
}

void ChatController::rebuildCatalog()
{
	QVariantList groups;
	for (const ModelProviderGroup& group : m_directory.groups) {
		QVariantMap groupRow;
		groupRow.insert(QStringLiteral("id"), group.id);
		groupRow.insert(QStringLiteral("name"), group.name);

		QVariantList models;
		for (const ModelOption& option : group.models) {
			QVariantMap row;
			row.insert(QStringLiteral("id"), option.id);
			row.insert(QStringLiteral("name"), option.name);
			// 副标题：说明为空时退回 id（原版 ModelSelectorRow）
			row.insert(QStringLiteral("description"),
				option.description.isEmpty() ? option.id : option.description);
			row.insert(QStringLiteral("selected"),
				m_directory.current.provider == group.id && m_directory.current.model == option.id);
			models.append(row);
		}
		groupRow.insert(QStringLiteral("models"), models);
		groups.append(groupRow);
	}
	m_modelGroups = groups;

	QVariantList levels;
	for (const ReasoningLevel& level : m_directory.selectableLevels()) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), level.id);
		row.insert(QStringLiteral("name"), level.name);
		row.insert(QStringLiteral("selected"), level.id == currentLevelId());
		levels.append(row);
	}
	m_reasoningLevels = levels;

	emit catalogChanged();
}

QString ChatController::currentLevelId() const
{
	const ReasoningLevel* level = m_directory.currentLevel();
	return level ? level->id : m_directory.current.reasoningEffort;
}

QString ChatController::currentModelName() const
{
	return m_directory.currentModelName();
}

QString ChatController::currentLevelName() const
{
	return m_directory.currentLevelName();
}

bool ChatController::useGenericLevels() const
{
	return m_directory.usesFallbackLevels();
}

bool ChatController::hasModels() const
{
	return m_catalogLoaded && !m_directory.groups.isEmpty();
}

void ChatController::selectModel(const QString& provider, const QString& model)
{
	if (!m_api || m_sessionId.isEmpty() || provider.isEmpty() || model.isEmpty())
		return;

	ModelSelection selection;  // 换模型清空档位（交给新模型的默认档位），成功后重拉目录
	selection.provider = provider;
	selection.model = model;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideProvider = selected.provider;
			m_overrideModel = selected.model;
			m_overrideEffort = selected.reasoningEffort;
			refreshModelCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel failed:" << error.code << error.message;
			refreshModelCatalog();
		});
}

void ChatController::selectLevel(const QString& levelId)
{
	if (!m_api || m_sessionId.isEmpty() || !m_directory.current.isValid())
		return;

	// 换档位保留 provider/model，只改 reasoningEffort，不重拉目录
	ModelSelection selection = m_directory.current;
	selection.reasoningEffort = levelId;

	ModelSelectionService::select(m_api, m_sessionId, selection,
		[this](const ModelSelection& selected) {
			m_overrideEffort = selected.reasoningEffort;
			m_directory.current.reasoningEffort = selected.reasoningEffort;
			rebuildCatalog();
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Chat] session/selectModel (level) failed:" << error.code << error.message;
		});
}

void ChatController::applySessionModelSelection(const QString& provider, const QString& model, const QString& reasoningEffort)
{
	m_overrideProvider = provider;
	m_overrideModel = model;
	m_overrideEffort = reasoningEffort;

	if (!provider.isEmpty() && !model.isEmpty())
		refreshModelCatalog();
}
