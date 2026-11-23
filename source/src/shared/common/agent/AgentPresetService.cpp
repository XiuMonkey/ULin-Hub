#include "common/agent/AgentPresetService.h"

#include "network/SessionCommands.h"

// 只放需要 DshApiClient 的联网逻辑；纯解析/决策逻辑 inline 在 AgentPresetService.h，便于离线单元测试。

namespace
{
	const char* const kListMethod = "agentPresets/list";   // dsh 0.1.5：斜杠 endpoint
	const char* const kSettingsUpdateMethod = "settings/update";
}

namespace AgentPresetService
{
	void fetch(DshApiClient* api, const std::function<void(const QVector<AgentPreset>& presets)>& onLoaded, const std::function<void(const DshApiClient::RpcError& error)>& onError)
	{
		if (!api)
			return;

		api->callMethod(QLatin1String(kListMethod), SessionCommands::emptyArgs(),   // dsh 0.1.5：命名空间为复数 agentPresets，该端点无参数
			[onLoaded](const QJsonObject& value) {
				if (onLoaded)
					onLoaded(parsePresets(value));
			},
			[onError](const DshApiClient::RpcError& error) {
				if (onError)
					onError(error);
			});
	}

	void persistDefault(DshApiClient* api, const QString& presetId, const std::function<void(const QString& presetId)>& onSaved, const std::function<void(const DshApiClient::RpcError& error)>& onError)
	{
		// 空 id 不能写：服务端不校验 id 是否存在，写入后每次建会话都会以 agent-preset/not-found 失败。
		if (presetId.isEmpty()) {
			if (onError) {
				DshApiClient::RpcError error;
				error.code = QStringLiteral("invalid-request");
				error.message = qtTrId("settings_no_preset");
				onError(error);
			}
			return;
		}
		if (!api) {
			if (onError) {
				DshApiClient::RpcError error;
				error.code = QStringLiteral("invalid-request");
				error.message = qtTrId("settings_no_preset");
				onError(error);
			}
			return;
		}

		// settings/update 把 patch 浅合并进该命名空间 user 段，回包是最新视图；写成功即可，不解析回包。
		api->callMethod(QLatin1String(kSettingsUpdateMethod), SessionCommands::settingsUpdate(QLatin1String(kSettingsNamespace), defaultPatch(presetId)),
			[presetId, onSaved](const QJsonObject&) {
				if (onSaved)
					onSaved(presetId);
			},
			[onError](const DshApiClient::RpcError& error) {
				if (onError)
					onError(error);
			});
	}
}