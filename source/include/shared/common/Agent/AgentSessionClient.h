#pragma once

// Agent 会话编排：follow（实时订阅 + snapshot）与 page（一次性历史翻页）。
// 通讯层 DshApiClient 只负责 RPC / mux 流，本类负责 DSH 的 session/agent 语义：
//   · session/follow 的 address（普通会话 / 子代理）
//   · 首帧 snapshot / event / assistant-stream 的归一化
//   · job/list 名册随 follow 生命周期
//   · 断线重连后按原 address 重开 follow
// UI（ChatController）消费本类信号，不再直接持有 DshApiClient 的会话流状态。

#include "network/DshApiClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class DshApiClient;

class AgentSessionClient : public QObject
{
	Q_OBJECT

public:
	explicit AgentSessionClient(DshApiClient* api, QObject* parent = nullptr);

	void followSession(const QString& sessionId, const QString& parentSessionId = QString(),
		const QString& mode = QString());
	void unfollowSession();
	void pageSession(const QString& sessionId, int throughSeq, int maxMessages = 0, int beforeSeq = 0,
		std::function<void(const QJsonObject& value)> onSuccess = {},
		std::function<void(const DshApiClient::RpcError& error)> onError = {});

signals:
	void sessionSnapshotReady(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore);
	void sessionProjectionsReady(const QString& sessionId, int asOfSeq, const QJsonObject& values);
	void muxFrameReceived(const QJsonObject& frame);
	void jobRosterReady(const QJsonArray& jobs);
	void transportError(const QString& context, const QString& message);

private:
	void onGenericStreamItem(const QString& streamId, const QJsonValue& value);
	void onGenericStreamEnded(const QString& streamId);
	void onConnected();
	void openSessionStreams();
	void handleSessionItem(const QJsonObject& frame);
	void handleJobsItem(const QJsonObject& frame);
	void emitSessionEventFrame(const QString& sessionId, const QJsonObject& event);
	void clearStreamIds();

	DshApiClient* m_api = nullptr;
	QString m_sessionStreamId;
	QString m_jobsStreamId;
	QString m_followedSessionId;
	QString m_followedParentSessionId;
	QString m_followedMode;
	// 与旧 DshApiClient 相同的“愿望账”：mux 重连后按它重开。
	QString m_resumeFollowId;
	QString m_resumeFollowParent;
	QString m_resumeFollowMode;
};
