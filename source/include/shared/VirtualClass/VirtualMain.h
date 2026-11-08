#pragma once

// 主窗口 / 后端数据注入宿主接口。由 DshApiClient 实现（登记名 kMainWindow）。

#include <functional>

class QNetworkAccessManager;
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>
#include <qplugin.h>

class QWidget;

class VirtualMain
{
public:
	virtual ~VirtualMain() = default;

	// 宿主已鉴权的 HTTP 连接（带引擎会话 cookie）。
	virtual QNetworkAccessManager* AuthenticatedHttp() = 0;

	// 通用 RPC 转发；method 形如 '<namespace>/<method>'，不带 /api 前缀。
	virtual void CallApi(const char* owner, const QString& method, const QJsonValue& args,
		std::function<void(const QJsonValue& value)> done,
		std::function<void(const QString& code, const QString& message)> failed)
	{
		Q_UNUSED(owner);
		Q_UNUSED(method);
		Q_UNUSED(args);
		Q_UNUSED(done);
		Q_UNUSED(failed);
	}

	// 服务器地址。
	virtual QUrl ServerBaseUrl() = 0;
	virtual QUrl ServerLaunchUrl() = 0;
	virtual QString CurrentSessionId() = 0;

	// 连接/传输事件。
	virtual void HandleConnected() = 0;
	virtual void ForwardMuxFrame(const QJsonObject& frame) = 0;
	virtual void HandleTransportError(const QString& context, const QString& message) = 0;

	// 会话投影与快照。
	virtual void HandleSessionSnapshot(const QString& sessionId, int cursor, const QJsonArray& records, bool hasMore) = 0;
	virtual void HandleSessionProjections(const QString& sessionId, int asOfSeq, const QJsonObject& values) = 0;
	virtual void HandleSessionControlBaseline(const QJsonObject& projectionsBySession) = 0;
	virtual void HandleSessionProjectionChanged(const QString& sessionId, const QString& key, const QJsonValue& value, int seq) = 0;

	// 工作区。
	virtual void HandleWorkspaceSnapshot(const QJsonArray& items, const QJsonArray& archivedSessionIds) = 0;
	virtual void HandleWorkspaceUpserted(const QJsonObject& workspace) = 0;
	virtual void HandleWorkspaceRemoved(const QString& workspaceId) = 0;
	virtual void HandleWorkspaceReordered(const QStringList& workspaceIds) = 0;
	virtual void HandleWorkspaceArchiveChanged(const QJsonArray& archivedSessionIds) = 0;

	// 历史兼容桩：刻意不接 session/list，避免切走用户正在看的会话。
	virtual void ExternalRefreshSessionTitles() = 0;
	virtual void ExternalShowOverlay(QWidget* popup) = 0;
	virtual void ExternalHideOverlay(QWidget* popup) = 0;
};
Q_DECLARE_INTERFACE(VirtualMain, "com.ulinhub.VirtualMain/1.0")
