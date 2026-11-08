#include "common/Agent/AgentSessionClient.h"

#include "network/SessionCommands.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonValue>

AgentSessionClient::AgentSessionClient(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
{
	if (!m_api)
		return;
	connect(m_api, &DshApiClient::genericStreamItem,
		this, &AgentSessionClient::onGenericStreamItem);
	connect(m_api, &DshApiClient::genericStreamEnded,
		this, &AgentSessionClient::onGenericStreamEnded);
	connect(m_api, &DshApiClient::connected,
		this, &AgentSessionClient::onConnected);
	// 接管态：扩展直接调 VirtualMain::HandleSessionSnapshot / ForwardMuxFrame，
	// 通讯层只做信号转发；这里接到本类，保证接管与真实 DSH 走同一套 UI 消费入口。
	connect(m_api, &DshApiClient::sessionSnapshotReady,
		this, &AgentSessionClient::sessionSnapshotReady);
	connect(m_api, &DshApiClient::sessionProjectionsReady,
		this, &AgentSessionClient::sessionProjectionsReady);
	connect(m_api, &DshApiClient::muxFrameReceived,
		this, &AgentSessionClient::muxFrameReceived);
}

void AgentSessionClient::followSession(const QString& sessionId, const QString& parentSessionId,
	const QString& mode)
{
	if (!m_api || sessionId.isEmpty())
		return;
	// 同一 id + 同一 address 形状不重开。
	if (sessionId == m_followedSessionId && parentSessionId == m_followedParentSessionId
		&& mode == m_followedMode)
		return;

	unfollowSession();
	m_followedSessionId = sessionId;
	m_followedParentSessionId = parentSessionId;
	m_followedMode = mode;
	m_resumeFollowId = sessionId;
	m_resumeFollowParent = parentSessionId;
	m_resumeFollowMode = mode;

	qInfo().noquote() << "[AgentSession] follow target =" << sessionId
		<< (parentSessionId.isEmpty() ? QStringLiteral("(session)")
			: QStringLiteral("(subagent of %1, mode=%2)").arg(parentSessionId,
				mode.isEmpty() ? QStringLiteral("unknown") : mode));
	openSessionStreams();
}

void AgentSessionClient::unfollowSession()
{
	if (m_api) {
		m_api->closeGenericStream(m_sessionStreamId);
		m_api->closeGenericStream(m_jobsStreamId);
	}
	clearStreamIds();
	m_resumeFollowId.clear();
	m_resumeFollowParent.clear();
	m_resumeFollowMode.clear();
	m_followedSessionId.clear();
	m_followedParentSessionId.clear();
	m_followedMode.clear();
}

void AgentSessionClient::pageSession(const QString& sessionId, int throughSeq, int maxMessages,
	int beforeSeq, std::function<void(const QJsonObject&)> onSuccess,
	std::function<void(const DshApiClient::RpcError&)> onError)
{
	if (!m_api)
		return;
	const bool sameTarget = sessionId == m_followedSessionId;
	const QString parentSessionId = sameTarget ? m_followedParentSessionId : QString();
	const QString mode = sameTarget ? m_followedMode : QString();
	m_api->callMethod(QStringLiteral("session/page"),
		SessionCommands::sessionPage(sessionId, throughSeq, maxMessages, beforeSeq,
			parentSessionId, mode),
		std::move(onSuccess), std::move(onError));
}

void AgentSessionClient::openSessionStreams()
{
	if (!m_api || m_resumeFollowId.isEmpty())
		return;
	const QJsonObject followArgs = SessionCommands::sessionFollow(
		m_resumeFollowId, m_resumeFollowParent, m_resumeFollowMode);
	m_sessionStreamId = m_api->openGenericStream(QStringLiteral("session/follow"), followArgs);

	QJsonObject jobsRequest;
	jobsRequest.insert(QStringLiteral("sessionId"), m_resumeFollowId);
	QJsonObject jobsArgs;
	jobsArgs.insert(QStringLiteral("request"), jobsRequest);
	m_jobsStreamId = m_api->openGenericStream(QStringLiteral("job/list"), jobsArgs);
}

void AgentSessionClient::clearStreamIds()
{
	m_sessionStreamId.clear();
	m_jobsStreamId.clear();
}

void AgentSessionClient::onConnected()
{
	// mux 重连：旧流已经随连接消失，按愿望账重开。首次连接时 resume 为空，不做事。
	if (!m_resumeFollowId.isEmpty())
		openSessionStreams();
}

void AgentSessionClient::onGenericStreamItem(const QString& streamId, const QJsonValue& value)
{
	if (streamId == m_sessionStreamId) {
		handleSessionItem(value.toObject());
		return;
	}
	if (streamId == m_jobsStreamId) {
		handleJobsItem(value.toObject());
		return;
	}
}

void AgentSessionClient::onGenericStreamEnded(const QString& streamId)
{
	if (streamId == m_sessionStreamId)
		m_sessionStreamId.clear();
	if (streamId == m_jobsStreamId) {
		m_jobsStreamId.clear();
		emit jobRosterReady(QJsonArray());
	}
}

void AgentSessionClient::handleSessionItem(const QJsonObject& frame)
{
	const QString type = frame.value(QStringLiteral("type")).toString();
	const QString sessionId = m_followedSessionId;

	if (type == QStringLiteral("snapshot")) {
		const int cursor = frame.value(QStringLiteral("cursor")).toInt();
		const QJsonArray records = SessionCommands::legacyRecords(
			frame.value(QStringLiteral("records")).toArray());
		const bool hasMore = frame.value(QStringLiteral("hasMore")).toBool();

		qInfo().noquote() << "[AgentSession] session snapshot sessionId=" << sessionId
			<< "cursor=" << cursor << "records=" << records.size() << "hasMore=" << hasMore;

		const QJsonObject projections = frame.value(QStringLiteral("projections")).toObject();
		if (!projections.isEmpty()) {
			emit sessionProjectionsReady(sessionId,
				projections.value(QStringLiteral("asOfSeq")).toInt(),
				projections.value(QStringLiteral("values")).toObject());
		}
		emit sessionSnapshotReady(sessionId, cursor, records, hasMore);
		return;
	}

	if (type == QStringLiteral("event")) {
		const QJsonObject event = frame.value(QStringLiteral("event")).toObject();
		if (event.isEmpty())
			return;
		const QJsonObject surfaceOp = event.value(QStringLiteral("surfaceOp")).toObject();
		if (surfaceOp.value(QStringLiteral("op")).toString() == QStringLiteral("replace")) {
			const int cursor = event.value(QStringLiteral("seq")).toInt();
			pageSession(sessionId, cursor, 200, 0,
				[this, sessionId, cursor](const QJsonObject& page) {
					const QJsonArray records = SessionCommands::legacyRecords(
						page.value(QStringLiteral("records")).toArray());
					emit sessionSnapshotReady(sessionId, cursor, records,
						page.value(QStringLiteral("hasMore")).toBool());
				},
				[this](const DshApiClient::RpcError& error) {
					qWarning().noquote() << "[AgentSession] replacement page refresh failed:"
						<< error.code << error.message;
				});
			return;
		}
		emitSessionEventFrame(sessionId, event);
		return;
	}

	if (type == QStringLiteral("assistant-stream")) {
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

void AgentSessionClient::handleJobsItem(const QJsonObject& frame)
{
	if (frame.value(QStringLiteral("type")).toString() != QStringLiteral("rows"))
		return;
	emit jobRosterReady(frame.value(QStringLiteral("jobs")).toArray());
}

void AgentSessionClient::emitSessionEventFrame(const QString& sessionId, const QJsonObject& event)
{
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
