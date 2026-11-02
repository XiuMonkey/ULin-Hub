// HTTP 请求 + mux WebSocket：生成 rpcId、拆信封后回调成败

#include "network/DshApiClient.h"
#include "network/SessionCommands.h"
#include "common/util/ConnectionManager.h"
#include "common/util/HostIndex.h"
#include "common/util/CommonRegistry.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocket>
#include <QDebug>
#include <QWidget>   // ExternalShow/HideOverlay 的签名里是 QWidget*（会解引用类型信息，必须完整类型）

DshApiClient::JsonParseRunnable::JsonParseRunnable(DshApiClient* client, QString rpcId, QByteArray body)
	: m_client(client)
	, m_rpcId(std::move(rpcId))
	, m_body(std::move(body))
{
}

void DshApiClient::JsonParseRunnable::run()   // 只做纯 JSON 解析、不碰回调，完成后回投主线程
{
	const QJsonDocument doc = QJsonDocument::fromJson(m_body);
	m_body.clear();
	if (m_client.isNull())
		return;
	QMetaObject::invokeMethod(m_client.data(),
		[client = m_client, rpcId = std::move(m_rpcId), doc]() {
			if (client)
				client->handleParsedResponse(rpcId, doc);
		},
		Qt::QueuedConnection);
}

DshApiClient::DshApiClient(QObject* parent)   // 只有 /api/remote.mux 一条 mux，各逻辑流都跑在它上面
	: QObject(parent)
	, m_nam(new QNetworkAccessManager(this))
	, m_stream(new QWebSocket)
{
	dshRegister("DshApiClient.001", m_stream, &QWebSocket::textMessageReceived, this, &DshApiClient::onStreamTextMessage);
	dshRegister("DshApiClient.002", m_stream, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		emit transportError(QStringLiteral("stream"), m_stream->errorString());
		qWarning().noquote() << "[DshApi] stream error:" << m_stream->errorString();
	});

	dshRegister("DshApiClient.003", m_stream, &QWebSocket::connected, this, [this] {
		m_streamConnected = true;
		m_reconnectDelayMs = 1000; // 连上即复位退避
		qInfo().noquote() << "[DshApi] mux connected -> opening $events + workspace/follow + session/control";
		m_eventsStreamId = nextStreamId(QStringLiteral("events"));
		m_workspaceStreamId = nextStreamId(QStringLiteral("workspace"));
		sendStreamOpen(QStringLiteral("$events"), m_eventsStreamId);
		sendStreamOpen(QStringLiteral("workspace/follow"), m_workspaceStreamId);
		m_controlStreamId = nextStreamId(QStringLiteral("control"));
		sendStreamOpen(QStringLiteral("session/control"), m_controlStreamId);

		// 重连后**必须重开 session/follow（+ job/list）**：openStreams() 里的 closeStreams() 已经把
		// m_followed* 清空了（那是"当前有没有活着的流"的账），所以这里只能看 m_resumeFollow*。
		// 两本账混成一本的后果：内置引擎重启（发布包首启会重启一次）或任何一次网络抖动之后，
		// follow 再也不打开 ⇒ 客户端收不到任何消息事件 ⇒ 用户看到"模型不回复"，
		// 而回合其实已经在服务端跑完了（session/*.jsonl 里有 assistant/message）。
		if (!m_resumeFollowId.isEmpty()) {
			const QString sessionId = m_resumeFollowId;
			const QString parentSessionId = m_resumeFollowParent;   // 子代理要按 subagent address 重开；丢了这两段就会退回 session 形状（引擎回 agent-busy）
			const QString mode = m_resumeFollowMode;
			m_followedSessionId.clear();
			m_followedParentSessionId.clear();
			m_followedMode.clear();
			m_sessionStreamId.clear();
			followSession(sessionId, parentSessionId, mode);
		}
		});
	dshRegister("DshApiClient.004", m_stream, &QWebSocket::disconnected, this, [this] {
		const int code = static_cast<int>(m_stream->closeCode());
		const QString reason = m_stream->closeReason();
		const bool wasConnected = m_streamConnected;
		m_streamConnected = false;
		m_eventsReady = false;
		qInfo().noquote() << "[DshApi] DSH stream disconnected closeCode=" << code
			<< "reason=" << reason << "wasConnected=" << wasConnected;
		if (!m_terminalStreamId.isEmpty()) {   // 终端附着流随 mux 一起没了：清账并告诉调用方（它会在 connected() 后用新 attachmentId 重开）
			const QString ended = m_terminalStreamId;
			m_terminalStreamId.clear();
			emit terminalStreamEnded(ended);
		}
		if (!m_jobsStreamId.isEmpty()) {
			m_jobsStreamId.clear();
			emit jobRosterReady(QJsonArray());
		}
		if (wasConnected)  // 必须自愈：服务端遇重复 streamId 会 close(1008) 关掉整条 mux
			scheduleReconnect();
		});

	m_takeoverSweep = new QTimer(this);  // 接管态回填超时扫描：扩展不回填时唯一能让调用方脱身的机制
	m_takeoverSweep->setInterval(5000);
	dshRegister("DshApiClient.006", m_takeoverSweep, &QTimer::timeout, this, &DshApiClient::sweepTakeoverTimeouts);
}

DshApiClient::~DshApiClient()
{
	m_destroyed = true;
	if (m_stream)
		disconnect(m_stream, nullptr, this, nullptr);
	closeStreams();
	delete m_stream;
}

// 只刷标题不动选择：见头文件那段说明 —— DshApiClient 这条线上没有对应端点，
// ShellController::refreshSessions() 会重建目录（可能换掉当前选中会话）⇒ 刻意不接。
void DshApiClient::ExternalRefreshSessionTitles()
{
	qInfo().noquote() << "[DshApi] ExternalRefreshSessionTitles ignored:"
		" QML 壳没有\"只刷标题不动选择\"的路（见 DshApiClient.h 注释）";
}

// 遮罩：QML 壳里对应 AppPopup + Scrim，宿主侧尚未提供（迁移计划 D1：先留无操作桩）。
// 扩展自建的顶层窗口不受影响，照常显示 —— 只是不吃宿主那层居中遮罩。
void DshApiClient::ExternalShowOverlay(QWidget* popup)
{
	Q_UNUSED(popup);
	qInfo().noquote() << "[DshApi] ExternalShowOverlay ignored: QML 壳的遮罩尚未实现"
		"（迁移计划 D1，扩展窗口照常显示）";
}

void DshApiClient::ExternalHideOverlay(QWidget* popup)
{
	Q_UNUSED(popup);
}

void DshApiClient::setBaseUrl(const QUrl& url)   // 服务端给的是带 ?token= 的认证 URL；令牌单独存，基础 URL 保持干净
{
	if (m_takenover) {  // 接管态忽略它：否则仍会做 token→cookie 握手、连真 DSH
		qInfo().noquote() << "[DshApi] setBaseUrl ignored (backend taken over):"
			<< url.toString(QUrl::RemoveQuery);
		return;
	}

	m_launchToken.clear();
	const QUrlQuery query(url);
	if (query.hasQueryItem(QStringLiteral("token")))
		m_launchToken = query.queryItemValue(QStringLiteral("token"));

	m_baseUrl = url;
	m_baseUrl.setQuery(QString());
	m_baseUrl.setFragment(QString());

	m_authCookie.clear();
	m_authenticated = false;

	qInfo().noquote() << "[DshApi] baseUrl set:" << m_baseUrl.toString()
		<< "launchToken:" << (m_launchToken.isEmpty() ? QStringLiteral("(none)") : QStringLiteral("present"));
}

QUrl DshApiClient::baseUrl() const
{
	if (m_takenover)  // 接管态下恒为空：那条 DSH 已经不在了，回显只会误导用户
		return QUrl();
	return m_baseUrl;
}

void DshApiClient::startAuthHandshake()   // 用启动令牌换 cookie；必须禁止自动跟随重定向，否则拿不到 Set-Cookie
{
	if (m_destroyed || m_authInFlight)
		return;

	if (m_launchToken.isEmpty()) {
		m_authenticated = true;
		openStreams();
		return;
	}

	m_authInFlight = true;
	const quint64 attempt = ++m_authAttempt;  // 握手代次：对不上的迟到回包直接丢，否则会覆盖新 cookie

	QUrl url = m_baseUrl;
	url.setPath(QStringLiteral("/"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("token"), m_launchToken);
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QVariant::fromValue(QNetworkRequest::ManualRedirectPolicy));
	request.setTransferTimeout(5000);  // 必须有超时：否则 m_authInFlight 永远为真、后续重试全被挡掉

	qInfo().noquote() << "[DshApi] auth handshake ->" << url.toString(QUrl::RemoveQuery) << "(token hidden)";
	QNetworkReply* reply = m_nam->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, attempt]() {
		reply->deleteLater();
		if (m_destroyed || attempt != m_authAttempt)
			return;
		m_authInFlight = false;

		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray header = reply->rawHeader("Set-Cookie");
		if (header.isEmpty()) {
			qWarning().noquote() << "[DshApi] auth handshake failed: HTTP" << status << reply->errorString();

			if (status == 0) {  // HTTP 0 = 传输层没连上，服务端重启期间属预期：只记日志 + 重连
				qInfo().noquote() << "[DshApi] auth handshake 瞬态失败（服务端多半在重启），稍后重试";
				scheduleReconnect();
				return;
			}

			failAuthQueue(QStringLiteral("auth"), qtTrId("server_auth_failed"));
			emit transportError(QStringLiteral("auth"), qtTrId("server_auth_failed"));
			return;
		}

		int end = header.indexOf(';');
		if (end < 0)
			end = header.size();
		m_authCookie = QString::fromUtf8(header.left(end)).trimmed();
		m_authenticated = true;
		qInfo().noquote() << "[DshApi] auth cookie acquired (" << m_authCookie.size() << "chars )";
		flushAuthQueue();  // 认证就绪：把等待中的 RPC 按顺序补发
		openStreams();
		});
}

void DshApiClient::flushAuthQueue()   // 补发走的还是 post()，此时已认证，不会再入队
{
	if (m_authQueue.isEmpty())
		return;
	qInfo().noquote() << "[DshApi] flush queued RPCs:" << m_authQueue.size();
	const QList<QueuedCall> queued = m_authQueue;
	m_authQueue.clear();
	for (const QueuedCall& call : queued)
		post(call.path, call.body, call.onSuccess, call.onError);
}

void DshApiClient::failAuthQueue(const QString& code, const QString& message)
{
	if (m_authQueue.isEmpty())
		return;
	const QList<QueuedCall> queued = m_authQueue;
	m_authQueue.clear();
	for (const QueuedCall& call : queued) {
		if (call.onError)
			call.onError(RpcError{ code, message });
	}
}

void DshApiClient::openStreams()   // 带令牌但没换到 cookie 时先握手（成功后回到这里）
{
	if (m_takenover) {  // 接管态流归扩展负责，宿主不开 mux
		qInfo().noquote() << "[DshApi] openStreams ignored (backend taken over)";
		return;
	}
	if (m_baseUrl.isEmpty()) {
		qWarning().noquote() << "[DshApi] openStreams ignored: baseUrl is empty";
		return;
	}
	if (!m_authenticated && !m_launchToken.isEmpty()) {
		qInfo().noquote() << "[DshApi] openStreams deferred: authenticating first";
		startAuthHandshake();
		return;
	}

	closeStreams();
	QNetworkRequest streamRequest(makeUrl(QStringLiteral("/api/remote.mux")));
	if (!m_authCookie.isEmpty())
		streamRequest.setRawHeader("Cookie", m_authCookie.toUtf8());
	m_streamClosing = false;  // 清掉"正在关闭"标记，重连逻辑才继续有效
	m_stream->open(streamRequest);
	qInfo().noquote() << "[DshApi] opening mux stream:" << m_baseUrl.toString();
}

QString DshApiClient::nextStreamId(const QString& prefix)   // 流 id 每次 open 必须唯一：重复 streamId 会让服务端 close(1008)
{
	++m_streamSeq;
	return QStringLiteral("%1-%2").arg(prefix).arg(m_streamSeq);
}

void DshApiClient::scheduleReconnect()
{
	if (!m_reconnectTimer) {
		m_reconnectTimer = new QTimer(this);
		m_reconnectTimer->setSingleShot(true);
		dshRegister("DshApiClient.005", m_reconnectTimer, &QTimer::timeout, this, [this] {
			if (m_destroyed || m_baseUrl.isEmpty() || m_streamClosing)
				return;
			qInfo().noquote() << "[DshApi] reconnecting mux…";
			m_authenticated = false;  // cookie 可能随连接代次失效，重连时重换一次更稳
			m_authCookie.clear();
			openStreams();
		});
	}
	if (m_reconnectTimer->isActive())
		return;

	const int delayMs = m_reconnectDelayMs;
	m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 8000);
	qInfo().noquote() << "[DshApi] mux reconnect scheduled in" << delayMs << "ms";
	m_reconnectTimer->start(delayMs);
}

void DshApiClient::closeStreams()
{
	qInfo().noquote() << "[DshApi] closing stream channel"
		<< (m_takenover ? QStringLiteral("(takenover: local bookkeeping only)") : QString());
	if (m_reconnectTimer)
		m_reconnectTimer->stop();
	if (!m_takenover) {  // 接管态不碰 WebSocket、不通知扩展：本路径会被析构调用
		m_streamClosing = true;
		if (m_stream)
			m_stream->close();
	}
	m_streamConnected = false;
	m_eventsReady = false;
	m_clientId.clear();
	m_eventsStreamId.clear();
	m_workspaceStreamId.clear();
	m_controlStreamId.clear();
	m_sessionStreamId.clear();
	m_terminalStreamId.clear();
	m_jobsStreamId.clear();
	m_followedSessionId.clear();
	m_followedParentSessionId.clear();
	m_followedMode.clear();
}

// open 帧：type / streamId / endpoint / payload.args
void DshApiClient::sendStreamOpen(const QString& endpoint, const QString& streamId, const QJsonObject& args)
{
	if (!m_streamConnected || !m_stream || streamId.isEmpty()) {
		qWarning().noquote() << "[DshApi] cannot open stream" << endpoint << "(mux not connected)";
		return;
	}

	QJsonObject payload;
	payload.insert(QStringLiteral("args"), args);
	QJsonObject frame;
	frame.insert(QStringLiteral("type"), QStringLiteral("open"));
	frame.insert(QStringLiteral("streamId"), streamId);
	frame.insert(QStringLiteral("endpoint"), endpoint);
	frame.insert(QStringLiteral("payload"), payload);

	qInfo().noquote() << "[DshApi] stream open ->" << endpoint << "streamId=" << streamId;
	m_stream->sendTextMessage(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void DshApiClient::sendStreamCancel(const QString& streamId)
{
	if (!m_streamConnected || !m_stream || streamId.isEmpty())
		return;

	QJsonObject frame;
	frame.insert(QStringLiteral("type"), QStringLiteral("cancel"));
	frame.insert(QStringLiteral("streamId"), streamId);
	m_stream->sendTextMessage(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

// 参数形状：request.address = {kind, sessionId} 或子代理的 {kind:"subagent", parentSessionId,
// childSessionId, mode}（由 SessionCommands::sessionFollow 构造）。子代理**不能**用 session 形状：
// 引擎 validateAddress 对 origin == subagent 的会话回 session/agent-busy，流的 open 会成功但
// 一帧快照都不发 —— 正是"点开子代理页签消息区全空"的根因。
void DshApiClient::followSession(const QString& sessionId, const QString& parentSessionId,
	const QString& mode)
{
	if (sessionId.isEmpty())
		return;

	// 同一目标重复调用不重开流；父会话 id / mode 一起比，避免"同一个 id 但地址形状变了"被误判为重复
	if (sessionId == m_followedSessionId && parentSessionId == m_followedParentSessionId
		&& mode == m_followedMode)
		return;

	unfollowSession();

	const QJsonObject args = SessionCommands::sessionFollow(sessionId, parentSessionId, mode);
	m_followedSessionId = sessionId;
	m_followedParentSessionId = parentSessionId;
	m_followedMode = mode;
	m_resumeFollowId = sessionId;   // 愿望账：mux 断了也要记住"该跟谁"，重连时靠它重开（见 connected() 里那段）
	m_resumeFollowParent = parentSessionId;
	m_resumeFollowMode = mode;

	qInfo().noquote() << "[DshApi] follow target =" << sessionId
		<< (parentSessionId.isEmpty() ? QStringLiteral("(session)")
			: QStringLiteral("(subagent of %1, mode=%2)").arg(parentSessionId,
				mode.isEmpty() ? QStringLiteral("unknown") : mode));

	if (m_takenover) {  // 接管态不开真流，只把"要看哪个会话"告诉扩展（载荷同 open 帧）
		QJsonObject frame;
		frame.insert(QStringLiteral("endpoint"), QStringLiteral("session/follow"));
		frame.insert(QStringLiteral("args"), args);
		const QString rpcId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		sendToSink(rpcId, QStringLiteral("$takeover/stream-open"), frame);  // fire-and-forget：不入 m_pending、不等回填
		return;
	}

	m_sessionStreamId = nextStreamId(QStringLiteral("session"));
	sendStreamOpen(QStringLiteral("session/follow"), m_sessionStreamId, args);

	// 后台任务名册（job/list）：跟 session/follow 同生命周期开/关。参数是引擎 Remote 的命名参数
	// `request: JobListRequest`（payload.args = { request: { sessionId } }），帧是 JobListFrame。
	// ⚠️ 这条流只在该会话被跟随期间才需要：名册是"该会话能看到的任务集合"（含无主任务），
	// 换会话必须跟着换，否则顶栏计数会串会话。
	QJsonObject jobsRequest;
	jobsRequest.insert(QStringLiteral("sessionId"), sessionId);
	QJsonObject jobsArgs;
	jobsArgs.insert(QStringLiteral("request"), jobsRequest);
	m_jobsStreamId = nextStreamId(QStringLiteral("jobs"));
	sendStreamOpen(QStringLiteral("job/list"), m_jobsStreamId, jobsArgs);
}

// 读当前 follow 目标的一页记录。普通会话与子代理共用：子代理那两段来自 followSession 记住的地址。
// 调用方（ChatController 翻页 / handleSessionItem 的 replace 重拉）不需要再解析子代理关系。
void DshApiClient::pageSession(const QString& sessionId, int throughSeq, int maxMessages,
	int beforeSeq, std::function<void(const QJsonObject&)> onSuccess,
	std::function<void(const RpcError&)> onError)
{
	const bool sameTarget = sessionId == m_followedSessionId;
	const QString parentSessionId = sameTarget ? m_followedParentSessionId : QString();
	const QString mode = sameTarget ? m_followedMode : QString();

	callMethod(QStringLiteral("session/page"),
		SessionCommands::sessionPage(sessionId, throughSeq, maxMessages, beforeSeq,
			parentSessionId, mode),
		std::move(onSuccess), std::move(onError));
}

void DshApiClient::unfollowSession()
{
	m_resumeFollowId.clear();   // 真正"不跟了"：愿望账也一起清掉 —— 否则重连会把刚取消的会话又拉回来（用户已经切走了）
	m_resumeFollowParent.clear();
	m_resumeFollowMode.clear();

	if (m_takenover) {  // 接管态：通知扩展收掉 follow
		if (!m_followedSessionId.isEmpty()) {
			QJsonObject frame;
			frame.insert(QStringLiteral("endpoint"), QStringLiteral("session/follow"));
			QJsonObject args;
			frame.insert(QStringLiteral("args"), args);
			const QString rpcId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			sendToSink(rpcId, QStringLiteral("$takeover/stream-cancel"), frame);
		}
		m_sessionStreamId.clear();
		m_followedSessionId.clear();
		m_followedParentSessionId.clear();
		m_followedMode.clear();
		return;
	}

	if (!m_sessionStreamId.isEmpty())
		sendStreamCancel(m_sessionStreamId);
	m_sessionStreamId.clear();

	if (!m_jobsStreamId.isEmpty())   // 名册流跟着 follow 一起收：换会话时新的名册会由 followSession 重开
		sendStreamCancel(m_jobsStreamId);
	m_jobsStreamId.clear();

	m_followedSessionId.clear();
	m_followedParentSessionId.clear();
	m_followedMode.clear();
}

// 终端附着流：args 由 SessionCommands::terminalAttach(agentId, id) 给。
// 与 session/follow 的差别：这条流随"终端块"的开关走，不随会话切换自动跟着换，所以由调用方显式开关。
void DshApiClient::openTerminalStream(const QJsonObject& attachArgs)
{
	if (m_takenover) {  // 接管态的流归扩展，宿主不开（终端也不在接管协议里）
		qInfo().noquote() << "[DshApi] openTerminalStream ignored (backend taken over)";
		return;
	}

	if (attachArgs.isEmpty()) {
		qWarning().noquote() << "[DshApi] openTerminalStream ignored: empty args";
		return;
	}

	if (!m_streamConnected) {  // 还没连上：不缓存请求，调用方在 connected() 后自己再来一次（要换新 attachmentId）
		qInfo().noquote() << "[DshApi] openTerminalStream deferred: mux not connected";
		return;
	}

	if (!m_terminalStreamId.isEmpty())  // 同时只留一条：旧的那条先 cancel
		sendStreamCancel(m_terminalStreamId);

	m_terminalStreamId = nextStreamId(QStringLiteral("terminal"));
	sendStreamOpen(QStringLiteral("terminal/attach"), m_terminalStreamId, attachArgs);
	emit terminalStreamOpened(m_terminalStreamId);
}

void DshApiClient::sendTerminalUplink(const QJsonObject& item)
{
	if (!m_streamConnected || !m_stream || m_terminalStreamId.isEmpty() || item.isEmpty())
		return;

	QJsonObject frame;
	frame.insert(QStringLiteral("type"), QStringLiteral("item"));
	frame.insert(QStringLiteral("streamId"), m_terminalStreamId);
	frame.insert(QStringLiteral("value"), item);
	m_stream->sendTextMessage(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void DshApiClient::closeTerminalStream()
{
	if (m_terminalStreamId.isEmpty())
		return;

	sendStreamCancel(m_terminalStreamId);
	m_terminalStreamId.clear();   // 本端主动关：不发 terminalStreamEnded，调用方自己知道（发它会让"关块"流程再走一遍收尾）
}

bool DshApiClient::isConnected() const   // = mux 已打开且 $events 已收到 ready 帧
{
	if (m_takenover)  // 接管态恒 true：传输归扩展，宿主没有"没连上"的判据
		return true;
	return m_streamConnected && m_eventsReady;
}

// POST /api/<method>；接管态的分流在 post() 里
void DshApiClient::callMethod(const QString& method, const QJsonObject& payload, std::function<void(const QJsonObject& value)> onSuccess, std::function<void(const RpcError& error)> onError)
{
	const QString rpcId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	QJsonObject argsPayload;  // payload 必须恰好只有一个 args 对象
	argsPayload.insert(QStringLiteral("args"), payload);
	QJsonObject body;
	body.insert(QStringLiteral("type"), QStringLiteral("client-request"));
	body.insert(QStringLiteral("rpcId"), rpcId);
	body.insert(QStringLiteral("method"), method);
	body.insert(QStringLiteral("payload"), argsPayload);

	qInfo().noquote() << "[DshApi] RPC ->" << method << " rpcId=" << rpcId;
	post(QStringLiteral("/api/") + method, body,
		[onSuccess](const QJsonValue& value) {
			if (onSuccess)
				onSuccess(value.toObject());
		},
		std::move(onError));
}

// 同 callMethod，但成功回调拿裸 JSON 值
void DshApiClient::callMethodValue(const QString& method, const QJsonObject& payload, std::function<void(const QJsonValue& value)> onSuccess, std::function<void(const RpcError& error)> onError)
{
	const QString rpcId = QUuid::createUuid().toString(QUuid::WithoutBraces);

	QJsonObject argsPayload;
	argsPayload.insert(QStringLiteral("args"), payload);
	QJsonObject body;
	body.insert(QStringLiteral("type"), QStringLiteral("client-request"));
	body.insert(QStringLiteral("rpcId"), rpcId);
	body.insert(QStringLiteral("method"), method);
	body.insert(QStringLiteral("payload"), argsPayload);

	qInfo().noquote() << "[DshApi] RPC ->" << method << " rpcId=" << rpcId;
	post(QStringLiteral("/api/") + method, body, std::move(onSuccess), std::move(onError));
}

// 同 callMethodValue，但 args 可以是数组（RPC 位置参数，如 settings/update(ns, patch, rev)）
void DshApiClient::callMethodArgs(const QString& method, const QJsonValue& args,
								  std::function<void(const QJsonValue& value)> onSuccess,
								  std::function<void(const RpcError& error)> onError)
{
	const QString rpcId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QJsonObject argsPayload;
	argsPayload.insert(QStringLiteral("args"), args);
	QJsonObject body;
	body.insert(QStringLiteral("type"), QStringLiteral("client-request"));
	body.insert(QStringLiteral("rpcId"), rpcId);
	body.insert(QStringLiteral("method"), method);
	body.insert(QStringLiteral("payload"), argsPayload);
	qInfo().noquote() << "[DshApi] RPC(args) ->" << method << " rpcId=" << rpcId;
	post(QStringLiteral("/api/") + method, body, std::move(onSuccess), std::move(onError));
}

// 通道 POST /api/$events/result；rpcId 就是帧里的 eventId
void DshApiClient::respond(const QString& rpcId, const QJsonObject& value, std::function<void(const QJsonObject& receipt)> onSuccess, std::function<void(const RpcError& error)> onError)
{
	respondValue(rpcId, QJsonValue(value), std::move(onSuccess), std::move(onError));
}

void DshApiClient::respondValue(const QString& rpcId, const QJsonValue& value, std::function<void(const QJsonObject& receipt)> onSuccess, std::function<void(const RpcError& error)> onError)
{
	if (!m_takenover && m_clientId.isEmpty()) {  // 接管态必须绕过：接管后 m_clientId 恒空，应答会被吞掉
		qWarning().noquote() << "[DshApi] respond ignored: $events stream not ready";
		if (onError) {
			RpcError error;
			error.code = QStringLiteral("stream-not-ready");
			error.message = qtTrId("server_stream_not_ready");
			onError(error);
		}
		return;
	}

	QJsonObject outcome;
	outcome.insert(QStringLiteral("kind"), QStringLiteral("result"));
	outcome.insert(QStringLiteral("value"), value);
	QJsonObject args;
	if (!m_clientId.isEmpty())  // 接管态 m_clientId 为空：干脆不带 clientId，而不是塞空串
		args.insert(QStringLiteral("clientId"), m_clientId);
	args.insert(QStringLiteral("eventId"), rpcId);
	args.insert(QStringLiteral("outcome"), outcome);

	qInfo().noquote() << "[DshApi] respond -> $events/result eventId=" << rpcId;
	callMethod(QStringLiteral("$events/result"), args, onSuccess, onError);
}

QUrl DshApiClient::makeUrl(const QString& path) const
{
	QUrl url = m_baseUrl;
	url.setPath(path);
	if (url.scheme() == QStringLiteral("https"))
		url.setScheme(QStringLiteral("wss"));
	else
		url.setScheme(QStringLiteral("ws"));
	return url;
}

// 回调存进 m_pending 供回包匹配，rpcId 记在 reply 上
void DshApiClient::post(const QString& path, const QJsonObject& body, std::function<void(const QJsonValue& value)> onSuccess, std::function<void(const RpcError& error)> onError)
{
	// 接管态分流写在这里：callMethod / callMethodValue / respond 都汇到本函数
	// 位置必须在认证队列之前，否则会排进 m_authQueue 等永不来的握手
	if (m_takenover) {
		const QJsonObject payload = body.value(QStringLiteral("payload")).toObject();
		dispatchTakeoverCall(body.value(QStringLiteral("rpcId")).toString(),
			body.value(QStringLiteral("method")).toString(),
			payload.value(QStringLiteral("args")).toObject(),
			std::move(onSuccess), std::move(onError));
		return;
	}

	if (!m_authenticated) {
		static constexpr int kMaxQueuedCalls = 256;
		if (m_authQueue.size() >= kMaxQueuedCalls) {
			qWarning().noquote() << "[DshApi] 认证未就绪、挂起队列已满，上限=" << kMaxQueuedCalls;   // 上限只进日志
			if (onError)
				onError(RpcError{ QStringLiteral("auth"), qtTrId("server_auth_queue_full") });
			return;
		}

		m_authQueue.append(QueuedCall{ path, body, std::move(onSuccess), std::move(onError) });
		qInfo().noquote() << "[DshApi] RPC queued until authenticated:" << path
			<< "queued=" << m_authQueue.size();
		if (!m_authInFlight)
			startAuthHandshake();
		return;
	}

	const QString rpcId = body.value(QStringLiteral("rpcId")).toString();
	m_pending.insert(rpcId, PendingCall{ path, std::move(onSuccess), std::move(onError), });

	QUrl url = m_baseUrl;
	url.setPath(path);
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	if (!m_authCookie.isEmpty())  // /api 需要认证 cookie，缺失时服务端直接 401
		request.setRawHeader("Cookie", m_authCookie.toUtf8());
	QNetworkReply* reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	reply->setProperty("rpcId", rpcId);
	connect(reply, &QNetworkReply::finished, this, &DshApiClient::onReplyFinished);
}

void DshApiClient::onReplyFinished()
{
	QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
	if (!reply)
		return;

	if (m_destroyed) {  // 析构期间忽略回调，避免调用已析构的接收方
		reply->deleteLater();
		return;
	}

	const QString rpcId = reply->property("rpcId").toString();
	const auto it = m_pending.constFind(rpcId);
	if (it == m_pending.constEnd()) {
		qWarning().noquote() << "[DshApi] unknown rpcId:" << rpcId;
		reply->deleteLater();
		return;
	}

	PendingCall pending = it.value();
	m_pending.erase(it);

	if (reply->error() != QNetworkReply::NoError) {
		// 独立语句：打 HTTP 状态码 + 响应正文。401 的正文会说明服务端要哪种凭据（别插进下面的 << 链里）。
		if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
		    const int probeStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		    const QString probeBody = QString::fromUtf8(reply->readAll()).left(400);
		    qWarning().noquote() << "[DshApi] HTTP status=" << probeStatus << " body=" << probeBody;
		}
		qWarning().noquote() << "[DshApi] HTTP transport error path=" << pending.path
			<< " rpcId=" << rpcId << " url=" << reply->url().toString() << " error=" << reply->errorString();
		if (pending.onError) {
			pending.onError(RpcError{ QStringLiteral("transport"), reply->errorString(), });
		}
		reply->deleteLater();
		return;
	}

	const QByteArray body = reply->readAll();  // 解析挪到线程池：大回包 fromJson 会卡主线程
	reply->deleteLater();

	m_parsing.insert(rpcId, pending);
	auto* task = new JsonParseRunnable(this, rpcId, body);
	QThreadPool::globalInstance()->start(task);
}

// 主线程：拆 result 信封并调回调。
// 接管路径的回填不走这里（它只登记在 m_pending），复用会静默丢回调
void DshApiClient::handleParsedResponse(const QString& rpcId, const QJsonDocument& doc)
{
	if (m_destroyed)
		return;

	const auto it = m_parsing.constFind(rpcId);
	if (it == m_parsing.constEnd()) {
		qWarning().noquote() << "[DshApi] stale parsed response rpcId=" << rpcId;
		return;
	}
	PendingCall pending = it.value();
	m_parsing.erase(it);

	const QJsonObject root = doc.object();
	qInfo().noquote() << "[DshApi] HTTP response path=" << pending.path << " rpcId=" << rpcId;

	const QJsonObject result = root.value(QStringLiteral("result")).toObject();
	if (result.value(QStringLiteral("ok")).toBool()) {
		if (pending.onSuccess)
			pending.onSuccess(result.value(QStringLiteral("value")));
	}
	else {
		const QJsonObject errorObj = result.value(QStringLiteral("error")).toObject();
		if (pending.onError) {
			pending.onError(RpcError{ errorObj.value(QStringLiteral("code")).toString(),
				errorObj.value(QStringLiteral("message")).toString(), });
		}
	}
}

// 分工：插件 → 宿主走本类实现的 VirtualApiHost；宿主 → 插件走 VirtualApiSink::OnOutboundRequest，
// 回填只能靠 rpcId（std::function 不是 metatype，传不出去）

VirtualApiSink* DshApiClient::apiSink() const   // 现取不缓存：注册表的值是 QPointer，插件 unload 时会自动置空
{
	QObject* const root = CommonRegistry::instance().FindFromRegistry(QString::fromUtf8(DshHostIndex::kApiSink)).data();
	if (!root)
		return nullptr;
	return qobject_cast<VirtualApiSink*>(root);
}

bool DshApiClient::sendToSink(const QString& rpcId, const QString& method, const QJsonObject& args)   // 返回 false = 没有接收端，调用方应当场失败掉这条请求
{
	VirtualApiSink* const sink = apiSink();
	if (!sink) {
		qWarning().noquote() << "[DshApi] 接管态出站失败：注册表里没有实现 VirtualApiSink 的扩展"
			" method=" << method << "rpcId=" << rpcId;
		return false;
	}

	const QByteArray rpcIdUtf8 = rpcId.toUtf8();
	const QByteArray methodUtf8 = method.toUtf8();
	const QByteArray argsUtf8 = QJsonDocument(args).toJson(QJsonDocument::Compact);

	qInfo().noquote() << "[DshApi] 接管态出站 ->" << method << "rpcId=" << rpcId;
	sink->OnOutboundRequest(rpcIdUtf8.constData(), methodUtf8.constData(), argsUtf8.constData());  // 三个指针只在本次调用期间有效，扩展要自己拷贝
	return true;
}

void DshApiClient::dispatchTakeoverCall(const QString& rpcId, const QString& method, const QJsonObject& args, std::function<void(const QJsonValue&)> onSuccess, std::function<void(const RpcError&)> onError)
{
	if (rpcId.isEmpty()) {
		qWarning().noquote() << "[DshApi] 接管态出站请求缺 rpcId，已丢弃 method=" << method;
		if (onError)
			onError(RpcError{ QStringLiteral("takenover-no-rpcid"),
				QStringLiteral("outbound request has no rpcId") });
		return;
	}

	PendingCall pending;
	pending.path = QStringLiteral("takeover:") + method;  // path 只用于日志，标明它没走过 HTTP
	pending.onSuccess = std::move(onSuccess);
	pending.onError = std::move(onError);
	pending.takeover = true;
	pending.takeoverDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kTakeoverCallTimeoutMs;
	m_pending.insert(rpcId, pending);

	if (!sendToSink(rpcId, method, args)) {  // 没有接收端：当场用错误收尾
		failPending(rpcId, QStringLiteral("takenover-no-sink"),
			QStringLiteral("no client extension implements VirtualApiSink"));
	}
}

bool DshApiClient::failPending(const QString& rpcId, const QString& code, const QString& message)
{
	const auto it = m_pending.constFind(rpcId);
	if (it == m_pending.constEnd())
		return false;

	PendingCall pending = it.value();
	m_pending.erase(it);

	if (m_destroyed)  // 接管路径没有 HTTP 那道 m_destroyed 检查，更可能"已析构而回调才到"
		return true;

	qWarning().noquote() << "[DshApi] 接管态请求失败 path=" << pending.path
		<< "rpcId=" << rpcId << "code=" << code << "message=" << message;
	if (pending.onError)
		pending.onError(RpcError{ code, message });
	return true;
}

void DshApiClient::failTakeoverPending(const QString& code, const QString& message)
{
	QStringList pendingIds;
	for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
		if (it.value().takeover)
			pendingIds.append(it.key());
	}

	for (const QString& rpcId : pendingIds)
		failPending(rpcId, code, message);
}

void DshApiClient::sweepTakeoverTimeouts()   // 接管路径唯一的兜底：扩展不回填 = 调用方永久挂着且无任何报错
{
	if (!m_takenover || m_pending.isEmpty())
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	QStringList expired;
	for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
		const PendingCall& pending = it.value();
		if (pending.takeover && pending.takeoverDeadlineMs > 0 && now >= pending.takeoverDeadlineMs)
			expired.append(it.key());
	}

	for (const QString& rpcId : expired) {
		failPending(rpcId, QStringLiteral("takenover-timeout"),
			QStringLiteral("backend extension did not answer within %1 ms")
			.arg(kTakeoverCallTimeoutMs));
	}
}

// 进入接管态的一次性收尾：把内置 DSH 那边已经开始的事全部作废。
// 接管到来时内置服务端已起来过：baseUrlReady / 认证握手 / 开流可能都已发生
void DshApiClient::enterTakenoverState()
{
	m_injectConnectedEmitted = false;  // 每一轮接管都重新允许发一次"已连接"（见 HandleConnected 里的闸门）

	++m_authAttempt;  // ① 认证握手：判废在途那一代，清掉 cookie 与在途标记
	m_authInFlight = false;
	m_authenticated = false;
	m_authCookie.clear();

	if (m_reconnectTimer)  // ② 先置 m_streamClosing 再关 mux，否则会被当成意外断线并重连
		m_reconnectTimer->stop();
	m_streamClosing = true;
	if (m_stream)
		m_stream->close();
	closeStreams();  // 只清本地账本

	if (!m_authQueue.isEmpty()) {  // ③ 还在等认证的请求不会走接管路径，按新后端重新下发
		const QList<QueuedCall> queued = m_authQueue;
		m_authQueue.clear();
		qInfo().noquote() << "[DshApi] 接管态：把" << queued.size() << "条等待认证的请求改投扩展";
		for (const QueuedCall& call : queued) {
			const QJsonObject payload = call.body.value(QStringLiteral("payload")).toObject();
			dispatchTakeoverCall(call.body.value(QStringLiteral("rpcId")).toString(),
				call.body.value(QStringLiteral("method")).toString(),
				payload.value(QStringLiteral("args")).toObject(),
				call.onSuccess, call.onError);
		}
	}

	if (m_takeoverSweep)  // ④ 超时扫断开跑
		m_takeoverSweep->start();
}

void DshApiClient::leaveTakenoverState()   // 刻意不重启内置 DSH：要不要拉回来由用户决定
{
	if (m_takeoverSweep)
		m_takeoverSweep->stop();

	failTakeoverPending(QStringLiteral("takenover-released"),
		QStringLiteral("backend takeover released by the extension"));

	m_streamClosing = false;
}

void DshApiClient::handleEventsItem(const QJsonValue& value)   // waterfall 是需要回执的审批/提问，翻成旧帧交给 UI
{
	const QJsonObject item = value.toObject();
	const QString type = item.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("ready")) {
		m_clientId = item.value(QStringLiteral("clientId")).toString();
		m_eventsReady = true;
		qInfo().noquote() << "[DshApi] $events ready, clientId=" << m_clientId;
		emit connected();
		return;
	}

	if (type == QStringLiteral("emit")) {
		qInfo().noquote() << "[DshApi] emit event:" << item.value(QStringLiteral("event")).toString();
		return;
	}

	if (type == QStringLiteral("waterfall")) {
		const QString event = item.value(QStringLiteral("event")).toString();
		const QString eventId = item.value(QStringLiteral("eventId")).toString();
		const QString agentId = item.value(QStringLiteral("agentId")).toString();

		QJsonObject payload = item.value(QStringLiteral("request")).toObject();
		if (!payload.contains(QStringLiteral("sessionId")) && !agentId.isEmpty())  // 帧上的 agentId 就是 SessionId，请求体没带时补上
			payload.insert(QStringLiteral("sessionId"), agentId);

		QString legacyType;
		if (event == QStringLiteral("approval/request"))
			legacyType = QStringLiteral("approval/requested");
		else if (event == QStringLiteral("user-questions/request"))
			legacyType = QStringLiteral("question/requested");
		if (legacyType.isEmpty())
			return;
		payload.insert(QStringLiteral("type"), legacyType);

		QJsonObject legacyFrame;
		legacyFrame.insert(QStringLiteral("rpcId"), eventId);
		legacyFrame.insert(QStringLiteral("payload"), payload);
		emit muxFrameReceived(legacyFrame);
	}
}

void DshApiClient::handleWorkspaceItem(const QJsonValue& value)
{
	const QJsonObject frame = value.toObject();
	const QString type = frame.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("baseline")) {
		const QJsonObject baseline = frame.value(QStringLiteral("value")).toObject();

		// 三个 payload 同源同帧：items + archivedSessionIds + pinnedSessionIds（置顶集合，保序、最近置顶的在前）。
		// ⚠️ pinnedSessionIds 必须一起转：漏掉它侧栏"置顶"段永远出不来。
		emit workspaceSnapshotReady(
			baseline.value(QStringLiteral("items")).toArray(),
			baseline.value(QStringLiteral("archivedSessionIds")).toArray(),
			baseline.value(QStringLiteral("pinnedSessionIds")).toArray());
		return;
	}

	if (type == QStringLiteral("upsert")) {
		emit workspaceUpserted(frame.value(QStringLiteral("workspace")).toObject());
		return;
	}

	if (type == QStringLiteral("remove")) {
		emit workspaceRemoved(frame.value(QStringLiteral("workspaceId")).toString());
		return;
	}

	if (type == QStringLiteral("archived")) {
		emit workspaceArchiveChanged(frame.value(QStringLiteral("archivedSessionIds")).toArray());
		return;
	}

	if (type == QStringLiteral("pinned")) {   // 置顶集合增量帧：帧里是完整集合（不是 diff），接收方整体替换即可
		emit workspacePinnedChanged(frame.value(QStringLiteral("pinnedSessionIds")).toArray());
		return;
	}

	if (type == QStringLiteral("order")) {
		QStringList workspaceIds;
		for (const auto& value : frame.value(QStringLiteral("workspaceIds")).toArray())
			workspaceIds.append(value.toString());
		emit workspaceReordered(workspaceIds);
		return;
	}
}

void DshApiClient::handleControlItem(const QJsonValue& value)   // baseline 是活会话的现算投影快照，之后靠 projection 帧推送；冷会话由 follow 快照兜底
{
	const QJsonObject frame = value.toObject();
	const QString type = frame.value(QStringLiteral("type")).toString();

	if (type == QStringLiteral("baseline")) {
		const QJsonObject baseline = frame.value(QStringLiteral("value")).toObject();
		const QJsonObject projections = baseline.value(QStringLiteral("projections")).toObject();
		qInfo().noquote() << "[DshApi] session/control baseline sessions=" << projections.size();
		if (!projections.isEmpty())
			emit sessionProjectionsBaselineReady(projections);
		return;
	}

	if (type == QStringLiteral("projection")) {
		const QString sessionId = frame.value(QStringLiteral("sessionId")).toString();
		const QString key = frame.value(QStringLiteral("key")).toString();
		if (sessionId.isEmpty() || key.isEmpty())
			return;

		emit sessionProjectionChanged(sessionId, key,
			frame.value(QStringLiteral("value")),
			frame.value(QStringLiteral("seq")).toInt());
		return;
	}
}

void DshApiClient::handleSessionItem(const QJsonValue& value)   // 记录翻成旧的 session/event 帧，复用既有渲染路径
{
	const QJsonObject frame = value.toObject();
	const QString type = frame.value(QStringLiteral("type")).toString();
	const QString sessionId = m_followedSessionId;

	if (type == QStringLiteral("snapshot")) {
		const int cursor = frame.value(QStringLiteral("cursor")).toInt();
		const QJsonArray records = SessionCommands::legacyRecords(
			frame.value(QStringLiteral("records")).toArray());
		const bool hasMore = frame.value(QStringLiteral("hasMore")).toBool();

		qInfo().noquote() << "[DshApi] session snapshot sessionId=" << sessionId
			<< "cursor=" << cursor << "records=" << records.size() << "hasMore=" << hasMore;

		const QJsonObject projections = frame.value(QStringLiteral("projections")).toObject();  // 快照还带一份全量投影（projectionMode: all）
		if (!projections.isEmpty()) {
			emit sessionProjectionsReady(sessionId,
				projections.value(QStringLiteral("asOfSeq")).toInt(),
				projections.value(QStringLiteral("values")).toObject());
		}

		emit sessionSnapshotReady(sessionId, cursor, records, hasMore);  // 整包交给 HistoryLoader 批量播种，否则历史会被渲染两遍
		return;
	}

	if (type == QStringLiteral("event")) {
		const QJsonObject event = frame.value(QStringLiteral("event")).toObject();
		if (event.isEmpty())
			return;
		const QJsonObject surfaceOp = event.value(QStringLiteral("surfaceOp")).toObject();
		if (surfaceOp.value(QStringLiteral("op")).toString() == QStringLiteral("replace")) {
			// replace 会改写旧 UI 已渲染的消息：从权威页重建，让旧文本与流式分片消失。
			// 走 pageSession：子代理会自动带 subagent address（裸 session/page 会被引擎拒）。
			const int cursor = event.value(QStringLiteral("seq")).toInt();
			pageSession(sessionId, cursor, 200, 0,
				[this, sessionId, cursor](const QJsonObject& page) {
					const QJsonArray records = SessionCommands::legacyRecords(
						page.value(QStringLiteral("records")).toArray());
					emit sessionSnapshotReady(sessionId, cursor, records,
						page.value(QStringLiteral("hasMore")).toBool());
				},
				[this](const RpcError& error) {
					qWarning().noquote() << "[DshApi] replacement page refresh failed:"
						<< error.code << error.message;
				});
			return;
		}
		emitSessionEventFrame(sessionId, event);
		return;
	}

	if (type == QStringLiteral("assistant-stream")) {  // chunk 是 LLM 原始增量，翻成 assistant/chunk 后与历史回放共用渲染路径
		const QJsonObject streamFrame = frame.value(QStringLiteral("frame")).toObject();
		if (streamFrame.value(QStringLiteral("type")).toString() == QStringLiteral("chunk")) {
			const QJsonObject chunk = streamFrame.value(QStringLiteral("chunk")).toObject();
			if (!chunk.isEmpty()) {
				QJsonObject event;
				event.insert(QStringLiteral("type"), QStringLiteral("assistant/chunk"));
				QJsonObject data;
				data.insert(QStringLiteral("chunk"), chunk);
				event.insert(QStringLiteral("data"), data);
				emitSessionEventFrame(sessionId, event);
			}
		}
	}
}

// terminal/follow 的帧就是终端帧本体（{type:"snapshot"|"output"|"state", …}），
// 这里不做任何解释：解析/渲染都在页面里（xterm.js），C++ 侧只负责搬。
void DshApiClient::handleTerminalItem(const QJsonValue& value)
{
	const QJsonObject frame = value.toObject();
	if (frame.isEmpty())
		return;

	emit terminalFrame(m_terminalStreamId, frame);
}

// 第六条逻辑流：job/list 的帧是 JobListFrame（{type:"rows", jobs:[...]}）。
// 这里不解包 JobView —— 字段解释与排序都在 ChatController（common 层），网络层只搬原始数组。
void DshApiClient::handleJobsItem(const QJsonValue& value)
{
	const QJsonObject frame = value.toObject();
	if (frame.value(QStringLiteral("type")).toString() != QStringLiteral("rows"))
		return;

	emit jobRosterReady(frame.value(QStringLiteral("jobs")).toArray());
}

void DshApiClient::emitSessionEventFrame(const QString& sessionId, const QJsonObject& event){
	QJsonArray record;
	QJsonObject wrapped;
	wrapped.insert(QStringLiteral("event"), event);
	record.append(wrapped);
	const QJsonArray normalized = SessionCommands::eventsFromRecords(record);
	const QJsonObject visibleEvent = normalized.isEmpty() ? event : normalized.first().toObject();

	QJsonObject payload;
	payload.insert(QStringLiteral("type"), QStringLiteral("session/event"));
	payload.insert(QStringLiteral("sessionId"), sessionId);
	payload.insert(QStringLiteral("event"), visibleEvent);

	QJsonObject legacyFrame;
	legacyFrame.insert(QStringLiteral("payload"), payload);
	emit muxFrameReceived(legacyFrame);
}

void DshApiClient::onStreamTextMessage(const QString& message)
{
	const QJsonObject frame = QJsonDocument::fromJson(message.toUtf8()).object();
	const QString streamType = frame.value(QStringLiteral("type")).toString();
	const QString streamId = frame.value(QStringLiteral("streamId")).toString();
	if (frame.isEmpty()) {
		qWarning().noquote() << "[DshApi] empty frame received";
		return;
	}

	if (streamType == QStringLiteral("item")) {
		const QJsonValue streamValue = frame.value(QStringLiteral("value"));
		if (streamId == m_eventsStreamId)
			handleEventsItem(streamValue);
		else if (streamId == m_workspaceStreamId)
			handleWorkspaceItem(streamValue);
		else if (streamId == m_controlStreamId)
			handleControlItem(streamValue);
		else if (streamId == m_sessionStreamId)
			handleSessionItem(streamValue);
		else if (!m_terminalStreamId.isEmpty() && streamId == m_terminalStreamId)
			handleTerminalItem(streamValue);
		else if (!m_jobsStreamId.isEmpty() && streamId == m_jobsStreamId)
			handleJobsItem(streamValue);
	}
	else if (streamType == QStringLiteral("end")) {
		qInfo().noquote() << "[DshApi] stream ended:" << streamId;
		if (!m_terminalStreamId.isEmpty() && streamId == m_terminalStreamId) {
			m_terminalStreamId.clear();
			emit terminalStreamEnded(streamId);
		}
		if (!m_jobsStreamId.isEmpty() && streamId == m_jobsStreamId) {
			m_jobsStreamId.clear();
			emit jobRosterReady(QJsonArray());
		}
	}
	else if (streamType == QStringLiteral("error")) {
		const QJsonObject streamError = frame.value(QStringLiteral("error")).toObject();
		const QString code = streamError.value(QStringLiteral("code")).toString();
		const QString detail = streamError.value(QStringLiteral("message")).toString();
		qWarning().noquote() << "[DshApi] stream error" << streamId << code << detail;

		// 终端流的错误按流级上抛（terminal/unavailable、terminal/control-unavailable、gateway/* …），
		// 别混进通用的 transportError：那些错误不该被当成"传输断了"去驱动重连。
		if (!m_terminalStreamId.isEmpty() && streamId == m_terminalStreamId) {
			m_terminalStreamId.clear();
			emit terminalStreamError(streamId, code, detail);
		}

		// 名册流失败（典型：profile 里没挂 job-controller ⇒ gateway/service-unavailable）：
		// 发一次空名册让顶栏退回空态，再走下面的通用 transportError 记到状态行。
		if (!m_jobsStreamId.isEmpty() && streamId == m_jobsStreamId) {
			m_jobsStreamId.clear();
			emit jobRosterReady(QJsonArray());
		}
		emit transportError(QStringLiteral("stream"), QStringLiteral("%1: %2").arg(code, detail));
	}
	else {
		qWarning().noquote() << "[DshApi] unknown mux frame type:" << streamType;
	}
}
