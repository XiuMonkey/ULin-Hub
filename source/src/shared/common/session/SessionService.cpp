#include "common/session/SessionService.h"

#include "common/agent/dsh/DshApiClient.h"
#include "network/SessionCommands.h"
#include "common/util/Logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <memory>

namespace
{
	const char* const kSessionListMethod = "session/list";
	const char* const kSessionCreateMethod = "session/create";
}

void SessionService::refreshSessions(DshApiClient* api, SessionCatalog* catalog,
	const std::function<void(const QString& autoSelectSessionId)>& onReady,
	const std::function<void(const DshApiClient::RpcError& error)>& onError)
{
	if (!api || !catalog)
		return;

	auto state = std::make_shared<SessionListSnapshot>();

	auto applySnapshot = [api, catalog, state, onReady, onError]() {
		catalog->applySnapshot(*state);
		if (!state->sessionsOk) {
			if (onError)
				onError(DshApiClient::RpcError{ state->errorCode, state->errorMessage });
			return;
		}

		const QString autoSelectId = catalog->autoSelectSessionId();

		TimingLogger::mark(QStringLiteral("session/list loaded (%1 items)").arg(state->sessions.size()));
		if (onReady)
			onReady(autoSelectId);
		};

	api->callMethod(QLatin1String(kSessionListMethod), SessionCommands::sessionList(),
		[state, applySnapshot](const QJsonObject& value) {
			const QJsonArray items = value.value(QStringLiteral("items")).toArray();
			state->sessions = items;
			state->sessionsOk = true;
			state->workspacesOk = false;   // workspacesOk 淇濇寔 false锛岃 catalog 淇濈暀 workspace/follow 缁欑殑鍩虹嚎鍒嗙粍
			applySnapshot();
		},
		[state, applySnapshot](const DshApiClient::RpcError& error) {
			state->errorCode = error.code;
			state->errorMessage = error.message;
			state->sessionsOk = false;
			applySnapshot();
		});
}

void SessionService::createSession(DshApiClient* api, const QString& workspaceId,
	const std::function<void(const QString& sessionId)>& onCreated,
	const std::function<void(const DshApiClient::RpcError& error)>& onError)
{
	if (!api)
		return;

	api->callMethod(QLatin1String(kSessionCreateMethod), SessionCommands::sessionCreate(workspaceId),
		[onCreated](const QJsonObject& value) {
			const QString sid = value.value(QStringLiteral("sessionId")).toString();
			if (sid.isEmpty())
				return;
			if (onCreated)
				onCreated(sid);
		},
		[onError](const DshApiClient::RpcError& error) {
			if (onError)
				onError(error);
		});
}

void SessionService::refreshTitles(DshApiClient* api,
	const std::function<void(const QString& sessionId, const QString& title)>& onTitle)
{
	if (!api)
		return;

	api->callMethod(QLatin1String(kSessionListMethod), SessionCommands::sessionList(),
		[onTitle](const QJsonObject& value) {
			const QJsonArray items = value.value(QStringLiteral("items")).toArray();
			for (const auto& item : items) {
				const QJsonObject session = item.toObject();
				const QString sid = session.value(QStringLiteral("sessionId")).toString();
				if (sid.isEmpty())
					continue;

				const QString label = SessionCatalog::projectionTitle(session);
				if (label.isEmpty())
					continue;
				if (onTitle)
					onTitle(sid, label);
			}
		},
		[](const DshApiClient::RpcError&) {});
}

void SessionService::refreshRunningStates(DshApiClient* api, SessionCatalog* catalog,
	const std::function<void(bool changed)>& onChanged)
{
	if (!api || !catalog)
		return;

	api->callMethod(QLatin1String(kSessionListMethod), SessionCommands::sessionList(),
		[catalog, onChanged](const QJsonObject& value) {
			const QJsonArray items = value.value(QStringLiteral("items")).toArray();
			const bool changed = catalog->applyRunningStates(items);
			if (onChanged)
				onChanged(changed);
		},
		[](const DshApiClient::RpcError&) {
			// 杞婚噺鍒锋柊澶辫触涓嶆墦鎵扮敤鎴凤細椤舵爮閭ｉ鐐逛繚鎸佹棫鍊硷紝绛変笅涓€娆¤Е鍙?
		});
}

void SessionService::deleteSessionData(const QString& dshHome, const QString& sessionId)
{
	if (dshHome.isEmpty() || sessionId.isEmpty())
		return;

	// 浼氳瘽鐩綍閫氬父褰㈠ dshHome/sessions/<encoded-cwd>/session-<id>锛涙寜鐩綍鍚嶇簿纭尮閰嶏紝
	// 涓嶈鍒犲悓鍓嶇紑鐨勫叾浠栦細璇濄€?
	const QString root = dshHome + QStringLiteral("/sessions");
	QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString path = it.next();
		if (QFileInfo(path).fileName() == sessionId) {
			QDir(path).removeRecursively();
			return;
		}
	}
}

void SessionService::clearAllSessionData(const QString& dshHome)
{
	if (dshHome.isEmpty())
		return;

	QDir sessionsDir(dshHome + QStringLiteral("/sessions"));
	if (sessionsDir.exists()) {
		sessionsDir.removeRecursively();
		sessionsDir.mkpath(QStringLiteral("."));
	}

	// 鏈杩愯鍐呯殑鍒嗙粍涓嶅彈褰卞搷 鈥斺€?娉ㄥ唽琛ㄦ椿鍦ㄦ湇鍔＄杩涚▼鍐呭瓨閲岋紝涓旀瘡娆″彉鏇撮兘浼氶噸鏂板啓鐩?
	// 锛坉sh-storage-json 鐨?putRecord 鈫?publish锛夛紝鎵€浠?杩炲綋鍓嶈繍琛岀殑宸ヤ綔鍖轰竴璧锋竻"瑕佽蛋 workspace/delete銆?
	// 榛樿宸ヤ綔鍖轰笉鍦ㄨ繖閲岃ˉ锛岀敱 ClientRuntime::resolveSessionWorkspace() 鍦ㄧ己鍒嗙粍鏃惰ˉ寤恒€?
	QFile::remove(dshHome + QStringLiteral("/storages/workspace.json"));
}
