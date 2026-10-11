// 见 SettingsController.h：设置页数据面。

#include "qmlbridge/SettingsController.h"

#include "qmlbridge/ThemeBridge.h"

#include "common/appearance/ThemeManager.h"
#include "common/appearance/TranslationManager.h"
#include "common/session/AgentPresetService.h"
#include "common/settings/SettingsStore.h"
#include "network/DshApiClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

SettingsController::SettingsController(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
{
	m_noticeTimer = new QTimer(this);  // 列表级瞬时提示：与原版 modelListNotice 一样 4 秒后自隐
	m_noticeTimer->setSingleShot(true);
	m_noticeTimer->setInterval(4000);
	connect(m_noticeTimer, &QTimer::timeout, this, [this]() {
		if (m_notice.isEmpty())
			return;
		m_notice.clear();
		emit noticeChanged();
		});

	// 语言列表必须在**任何 QML 绑定之前**就位。
	// openPage() 的顺序是"先 emit openChanged()（QML 的 Loader 同步开始装载设置页，
	// 语言下拉当场绑定 Hub.settings.languages）、后 refresh() → loadAppearance() 填数据"，
	// 所以在第一次打开设置页时绑定到的是空列表；配合 CONSTANT 属性就是"永远空白且不报错"。
	// Translation::init() 在 main.cpp 里早于 ShellController::init() 执行（main.cpp:45 < :70），
	// 所以这里调用是安全的，能拿到真实的语言包列表。
	loadAppearance();
}

void SettingsController::setNotice(const QString& text)
{
	m_notice = text;
	emit noticeChanged();
	if (!text.isEmpty())
		m_noticeTimer->start();
}

void SettingsController::openPage()
{
	if (m_open)
		return;

	m_open = true;
	emit openChanged();
	refresh();
}

void SettingsController::setPage(int page)
{
	const int bounded = qBound(0, page, 3);
	if (m_page == bounded)
		return;

	m_page = bounded;
	emit pageChanged();
}

void SettingsController::closePage()
{
	if (!m_open)
		return;

	m_open = false;
	m_discovered.clear();
	m_candidates.clear();
	emit openChanged();
	emit candidatesChanged();
}

void SettingsController::removeModel(const QString& provider, const QString& modelId)
{
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(QStringLiteral("找不到提供方 %1 的设置命名空间。").arg(provider));
		return;
	}

	setBusy(true);

	ModelSelectionService::removeModel(m_api, *entry, *ns, modelId,
		[this, modelId](const SettingsNamespace&) {
			setBusy(false);
			setNotice(qtTrId("model_deleted_fmt").arg(modelId));
			refresh();
		},
		[this, modelId](const DshApiClient::RpcError& error) {
			setBusy(false);
			setNotice(qtTrId("model_delete_failed_fmt").arg(modelId, error.code, error.message));
		});
}

void SettingsController::loadAgentPresets()
{
	if (!m_api)
		return;

	m_presetFeedback.clear();
	AgentPresetService::fetch(m_api,
		[this](const QVector<AgentPreset>& presets) {
			QVariantList rows;
			for (const AgentPreset& preset : presets) {
				QVariantMap row;
				row.insert(QStringLiteral("id"), preset.id);
				row.insert(QStringLiteral("name"), preset.name);
				rows.append(row);
			}
			m_agentPresets = rows;

			if (rows.isEmpty()) {
				m_defaultPresetName = qtTrId("settings_no_preset");
			}
			else {
				// 选中项完全由服务端定（每行的 isDefault）；客户端不存本地副本
				const QString selectedId = AgentPresetService::resolveSelectedId(presets, QString());
				m_defaultPresetName.clear();
				for (const AgentPreset& preset : presets) {
					if (preset.id == selectedId) {
						m_defaultPresetName = preset.name;
						break;
					}
				}
				if (m_defaultPresetName.isEmpty())
					m_defaultPresetName = presets.first().name;
			}
			emit presetsChanged();
		},
		[this](const DshApiClient::RpcError& error) {
			m_agentPresets.clear();
			m_defaultPresetName = qtTrId("common_load_failed_fmt").arg(error.code, error.message);
			emit presetsChanged();
		});
}

void SettingsController::selectDefaultPreset(const QString& presetId)
{
	if (!m_api || presetId.isEmpty())
		return;

	const QString previousName = m_defaultPresetName;  // 乐观显示（与旧壳同）：写入失败再退回原文字
	for (const QVariant& value : m_agentPresets) {
		const QVariantMap row = value.toMap();
		if (row.value(QStringLiteral("id")).toString() == presetId) {
			m_defaultPresetName = row.value(QStringLiteral("name")).toString();
			break;
		}
	}
	m_presetFeedback.clear();
	emit presetsChanged();

	AgentPresetService::persistDefault(m_api, presetId,
		[this](const QString& savedId) {
			// 旧壳只在成功时留一行日志：界面是乐观更新，没有"已保存"文案
			qInfo().noquote() << "[Settings] default agent preset ->" << savedId;
			m_presetFeedback.clear();
			emit presetsChanged();
		},
		[this, previousName](const DshApiClient::RpcError& error) {
			m_defaultPresetName = previousName;
			m_presetFeedback = qtTrId("common_load_failed_fmt").arg(error.code, error.message);
			emit presetsChanged();
		});
}

void SettingsController::saveServerUrl(const QString& url)
{
	SettingsStore::setServerUrl(url.trimmed());
	m_serverUrl = SettingsStore::serverUrl();
	emit serverUrlChanged();
	emit serverRestartRequested();  // 旧壳：Settings::serverSettingsSaved -> ServerManager::restart
}

void SettingsController::loadAppearance()
{
	QVariantList rows;
	QVariantMap follow;  // 第 0 项固定是"跟随系统"（数据为空串），与旧壳一致
	follow.insert(QStringLiteral("name"), qtTrId("settings_language_follow_system"));
	follow.insert(QStringLiteral("code"), QString());
	rows.append(follow);

	for (const LanguageInfo& language : Translation::availableLanguages()) {
		QVariantMap row;
		row.insert(QStringLiteral("name"), language.name);
		row.insert(QStringLiteral("code"), language.code);
		rows.append(row);
	}

	m_languages = rows;
	m_languageCode = Translation::savedLanguageCode();
	m_appearanceFeedback.clear();
	emit languageChanged();
}

void SettingsController::setLanguage(const QString& code)
{
	Translation::setSavedLanguageCode(code);
	m_languageCode = code;

	if (Translation::apply(code)) {
		m_appearanceFeedback = qtTrId("settings_language_switched");
	}
	else {
		// 语言包缺失：退回原文并把原因说清楚（不是错误）
		m_appearanceFeedback = qtTrId("settings_language_pack_missing_fmt")
			.arg(code.isEmpty() ? qtTrId("settings_language_system_name") : code);
	}
	emit languageChanged();
}

void SettingsController::resetStyles()
{
	ThemeManager::instance().resetStylePack();

	// 风格包重置后让 QML 侧热重载 token（同一模块，直接调；M8 前这条路由 ThemeManager 的信号触发）
	ThemeBridge::instance().reload();
	m_appearanceFeedback = qtTrId("settings_styles_reset_done");
	emit languageChanged();
}

const ConfigurableProvider* SettingsController::findProvider(const QString& provider) const
{
	return m_view.findProvider(provider);
}

const SettingsNamespace* SettingsController::findNamespace(const ConfigurableProvider& provider) const
{
	return m_view.findNamespace(provider.settingsNs);
}

void SettingsController::setBusy(bool busy)
{
	if (m_busy == busy)
		return;

	m_busy = busy;
	emit busyChanged();
}

void SettingsController::setStatus(const QString& text)
{
	if (m_status == text)
		return;

	m_status = text;
	emit statusChanged();
}

void SettingsController::setFormFeedback(const QString& text)
{
	if (m_formFeedback == text)
		return;

	m_formFeedback = text;
	emit formFeedbackChanged();
}

QString SettingsController::routeHint(const QString& provider) const
{
	// 与旧壳 ModelListPanel::routeHint 逐句一致
	const ConfigurableProvider* entry = findProvider(provider);
	if (!entry)
		return qtTrId("model_no_configurable_provider");

	QStringList parts;
	const SettingsNamespace* ns = findNamespace(*entry);
	if (!ns)
		return qtTrId("model_namespace_missing_fmt").arg(entry->settingsNs);

	const QJsonArray models = ModelSelectionService::configuredModels(*ns, entry->settingsPath);
	const bool userDeclared = ModelSelectionService::userDeclaresModels(*ns, entry->settingsPath);

	parts.append(qtTrId("model_write_progress_fmt").arg(entry->settingsNs,
		ModelSelectionService::modelsPath(*entry).join(QLatin1Char('.')),
		QString::number(models.size())));

	if (routeIsPiAi(provider)) {
		// pi-ai 的 models 是整体替换，会收窄公布范围
		if (!userDeclared)
			parts.append(qtTrId("model_write_warning_desc"));
	}
	else {
		parts.append(qtTrId("model_write_deepseek_warning"));
	}

	if (!m_view.settingsWritable)
		parts.append(qtTrId("model_write_readonly"));

	return parts.join(QLatin1Char(' '));
}

bool SettingsController::routeIsPiAi(const QString& provider) const
{
	const ConfigurableProvider* entry = findProvider(provider);
	return entry && entry->settingsNs.contains(QStringLiteral("pi-ai"));
}

void SettingsController::writeModel(const ConfigurableProvider& provider, const SettingsNamespace& ns, const AddModelRequest& request, bool replacing)
{
	setFormFeedback(qtTrId("model_writing"));
	ModelSelectionService::addModel(m_api, provider, ns, request,
		[this, request, replacing](const SettingsNamespace&) {
			setBusy(false);
			setFormFeedback(replacing
				? qtTrId("model_updated_fmt").arg(request.id)
				: qtTrId("model_added_fmt").arg(request.id));
			emit modelAdded(request.provider, request.id);
			refresh();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setFormFeedback(qtTrId("model_add_failed_fmt").arg(error.code, error.message));
		});
}

void SettingsController::submitModelForm(const QString& provider, const QString& modelId, const QString& displayName, const QString& contextWindow, const QString& maxTokens, const QString& efforts, const QString& apiKey)
{
	// 校验顺序与文案沿用旧壳（ModelListPanel::submitForm）
	if (provider.isEmpty()) {
		setFormFeedback(qtTrId("model_select_provider_first"));
		return;
	}
	if (modelId.trimmed().isEmpty()) {
		setFormFeedback(qtTrId("model_id_required"));
		return;
	}

	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!entry || !ns) {
		setFormFeedback(qtTrId("model_namespace_missing_fmt").arg(provider));
		return;
	}

	AddModelRequest request;
	request.provider = entry->provider;
	request.id = modelId.trimmed();
	request.name = displayName.trimmed();

	// 可选整数：空 = 不写；非法 = 拒绝（旧壳 parseOptionalInt）
	const auto parseOptional = [](const QString& text, bool* has, int* value) {
		const QString trimmed = text.trimmed();
		if (trimmed.isEmpty()) {
			*has = false;
			return true;
		}
		bool ok = false;
		const int parsed = trimmed.toInt(&ok);
		if (!ok || parsed <= 0)
			return false;
		*has = true;
		*value = parsed;
		return true;
		};

	if (!parseOptional(contextWindow, &request.hasContextWindow, &request.contextWindow)
		|| !parseOptional(maxTokens, &request.hasMaxTokens, &request.maxTokens)) {
		setFormFeedback(qtTrId("model_numeric_invalid"));
		return;
	}

	const bool piAi = routeIsPiAi(provider);
	if (piAi && !efforts.trimmed().isEmpty()) {
		QStringList available;
		for (const ReasoningLevel& level : fallbackReasoningLevels())
			available << level.id;

		const QStringList typed = efforts.split(QLatin1Char(','), Qt::SkipEmptyParts);
		for (const QString& raw : typed) {
			const QString id = raw.trimmed();
			if (id.isEmpty())
				continue;
			if (!available.contains(id)) {
				setFormFeedback(qtTrId("model_think_level_unknown_fmt").arg(id, available.join(QLatin1Char('/'))));
				return;
			}
			request.reasoningEfforts.append(id);
		}
	}

	// 凭据引用：profile 点名的优先，否则按约定派生并记入配置（旧壳 refreshCredentialRow + 1410 行）
	const QString declared = ModelSelectionService::profileApiKeyEnv(*ns, entry->settingsPath);
	const QString ref = declared.isEmpty()
		? ModelSelectionService::deriveKeyRef(entry->provider)
		: declared;

	bool replacing = false;  // 同 ID 已存在 = 覆盖（旧壳据此选 model_updated_fmt / model_added_fmt）
	const QJsonArray existing = ModelSelectionService::configuredModels(*ns, entry->settingsPath);
	for (const QJsonValue& value : existing) {
		if (value.toObject().value(QStringLiteral("id")).toString() == request.id) {
			replacing = true;
			break;
		}
	}

	setBusy(true);
	const QString typedKey = apiKey.trimmed();
	if (typedKey.isEmpty()) {
		writeModel(*entry, *ns, request, replacing);
		return;
	}

	request.apiKeyRef = ref;
	request.recordApiKeyEnv = declared.isEmpty() && !ref.isEmpty();
	setFormFeedback(qtTrId("model_writing_credential"));
	const ConfigurableProvider providerCopy = *entry;
	const SettingsNamespace nsCopy = *ns;
	ModelSelectionService::saveCredential(m_api, ref, typedKey,
		[this, providerCopy, nsCopy, request, replacing]() {
			writeModel(providerCopy, nsCopy, request, replacing);
		},
		[this, ref](const DshApiClient::RpcError& error) {
			setBusy(false);
			setFormFeedback(qtTrId("model_credential_write_failed_fmt").arg(ref, error.code, error.message));
		});
}

void SettingsController::refresh()
{
	if (!m_api)
		return;

	setBusy(true);
	setStatus(QString());

	loadAgentPresets();  // 同旧壳 Settings::refreshOnOpen：每次打开重同步服务端地址、预设、模型目录
	loadAppearance();
	m_serverUrl = SettingsStore::serverUrl();
	if (m_serverUrl.isEmpty())
		m_serverUrl = m_api->baseUrl().toString();
	emit serverUrlChanged();

	ModelSelectionService::fetchView(m_api,
		[this](const ServerModelView& view) {
			m_view = view;
			setBusy(false);
			rebuildProviders();

			m_credentials.clear();  // 逐个 provider 查凭据状态：配没配、可不可写、来源（env / file）
			for (const ConfigurableProvider& provider : m_view.providers) {
				const SettingsNamespace* ns = findNamespace(provider);
				if (!ns)
					continue;

				const QString ref = ModelSelectionService::resolveKeyRef(provider.provider, *ns, provider.settingsPath);
				if (ref.isEmpty())
					continue;

				ModelSelectionService::describeCredential(m_api, ref,
					[this](const CredentialStatus& status) {
						m_credentials.append(status);
						rebuildProviders();
					},
					[](const DshApiClient::RpcError& error) {
						qWarning().noquote() << "[Settings] credentials/describe 失败:" << error.code << error.message;
					});
			}

			if (m_view.providers.isEmpty())
				setStatus(QStringLiteral("服务端没有报告可配置的模型提供方。"));
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(QStringLiteral("读取设置失败：%1 %2").arg(error.code, error.message));
		});
}

void SettingsController::rebuildProviders()
{
	QVariantList rows;
	for (const ConfigurableProvider& provider : m_view.providers) {
		const SettingsNamespace* ns = findNamespace(provider);

		QVariantMap row;
		row.insert(QStringLiteral("provider"), provider.provider);
		row.insert(QStringLiteral("displayName"), provider.displayName);
		row.insert(QStringLiteral("settingsNs"), provider.settingsNs);
		row.insert(QStringLiteral("active"), provider.active);
		row.insert(QStringLiteral("declared"), provider.declared);

		// 下拉项文案：与旧壳 syncFormToRoute 同规则（displayName + 路由后缀 + 未启用后缀）
		QString label = provider.displayName;
		if (label != provider.provider)
			label += qtTrId("model_provider_route_suffix_fmt").arg(provider.provider);
		if (!provider.active)
			label += qtTrId("model_disabled_suffix");
		row.insert(QStringLiteral("label"), label);

		QString ref;
		bool declaredByProfile = false;
		if (ns) {
			declaredByProfile = !ModelSelectionService::profileApiKeyEnv(*ns, provider.settingsPath).isEmpty();
			ref = ModelSelectionService::resolveKeyRef(provider.provider, *ns, provider.settingsPath);
		}
		row.insert(QStringLiteral("keyRef"), ref);
		row.insert(QStringLiteral("keyDeclaredByProfile"), declaredByProfile);
		row.insert(QStringLiteral("namespaceFound"), ns != nullptr);

		// 凭据状态：还没查到时按"未知"呈现（不能当成只读）
		bool known = false;
		bool configured = false;
		bool writable = false;
		QString source;
		for (const CredentialStatus& status : m_credentials) {
			if (status.ref != ref || ref.isEmpty())
				continue;
			known = status.known;
			configured = status.configured;
			writable = status.writable;
			source = status.source;
			break;
		}
		row.insert(QStringLiteral("keyKnown"), known);
		row.insert(QStringLiteral("keyConfigured"), configured);
		row.insert(QStringLiteral("keyWritable"), writable);
		row.insert(QStringLiteral("keySource"), source);

		QVariantList models;  // 已声明的模型（settings 里的 models 数组，整份替换语义）
		if (ns) {
			const QJsonArray declared = ModelSelectionService::configuredModels(*ns, provider.settingsPath);
			for (const QJsonValue& value : declared) {
				ConfiguredModel model;
				if (!ModelSelectionService::parseConfiguredModel(value, &model))
					continue;
				QVariantMap entry;
				entry.insert(QStringLiteral("id"), model.id);
				entry.insert(QStringLiteral("name"), model.name);
				models.append(entry);
			}
		}
		row.insert(QStringLiteral("models"), models);
		rows.append(row);
	}

	m_providers = rows;

	// 模型页的行：与旧壳 ModelListPanel 一样，用 buildModelInfos 把"目录公布的模型"与
	// "settings 里声明的条目"join 成展示行（容量、写回地址只有 settings 侧知道）
	QVariantList modelRows;
	const QVector<ModelInfo> infos = ModelSelectionService::buildModelInfos(m_view);
	for (const ModelInfo& info : infos) {
		QVariantMap row;
		row.insert(QStringLiteral("provider"), info.provider);
		row.insert(QStringLiteral("providerName"), info.providerName);
		row.insert(QStringLiteral("id"), info.id);
		row.insert(QStringLiteral("name"), info.name);
		row.insert(QStringLiteral("description"), info.description);
		row.insert(QStringLiteral("declared"), info.declared);
		row.insert(QStringLiteral("userDeclared"), info.userDeclared);
		row.insert(QStringLiteral("hasContextWindow"), info.hasContextWindow);
		row.insert(QStringLiteral("contextWindow"), info.contextWindow);
		row.insert(QStringLiteral("hasMaxTokens"), info.hasMaxTokens);
		row.insert(QStringLiteral("maxTokens"), info.maxTokens);
		row.insert(QStringLiteral("hasReasoning"), info.hasReasoning);
		row.insert(QStringLiteral("reasoningDisabled"), info.reasoningDisabled);
		row.insert(QStringLiteral("settingsNs"), info.settingsNs);
		row.insert(QStringLiteral("settingsWritable"), info.settingsWritable);

		QStringList levels;
		for (const ReasoningLevel& level : info.levels)
			levels << level.name;
		row.insert(QStringLiteral("levels"), levels);
		modelRows.append(row);
	}
	m_models = modelRows;

	QVariantList failures;  // 失败项（原版状态行会逐个列出：提供方名 + 原因）
	for (const ModelCatalogFailure& failure : m_view.failures) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), failure.id);
		row.insert(QStringLiteral("name"), failure.name);
		row.insert(QStringLiteral("message"), failure.message);
		failures.append(row);
	}
	m_failures = failures;
	m_groupCount = m_view.groups.size();

	emit viewChanged();
}

void SettingsController::saveApiKey(const QString& provider, const QString& apiKey)
{
	if (!m_api || apiKey.trimmed().isEmpty()) {
		setStatus(QStringLiteral("请先填入 API key。"));
		return;
	}

	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!entry || !ns) {
		setStatus(QStringLiteral("找不到提供方 %1 的设置命名空间。").arg(provider));
		return;
	}

	const QString ref = ModelSelectionService::resolveKeyRef(entry->provider, *ns, entry->settingsPath);
	setBusy(true);
	ModelSelectionService::saveCredential(m_api, ref, apiKey.trimmed(),
		[this, ref]() {
			setBusy(false);
			setStatus(QStringLiteral("已写入凭据 %1。可以点「发现模型」拉取可用模型。").arg(ref));
			refresh();  // 重新查状态：界面上的"已配置"要立刻反映出来
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(QStringLiteral("写入凭据失败：%1 %2").arg(error.code, error.message));
		});
}

void SettingsController::discover(const QString& provider, const QString& apiKey)
{
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(QStringLiteral("找不到提供方 %1 的设置命名空间。").arg(provider));
		return;
	}

	setBusy(true);
	const QJsonObject request = ModelSelectionService::buildDiscoveryRequest(*entry, ns, apiKey);
	ModelSelectionService::discoverModels(m_api, entry->settingsNs, request,
		[this](const QVector<DiscoveredModel>& models) {
			setBusy(false);
			m_discovered = models;

			QVariantList rows;
			for (const DiscoveredModel& model : models) {
				QVariantMap row;
				row.insert(QStringLiteral("id"), model.id);
				row.insert(QStringLiteral("name"), model.name.isEmpty() ? model.id : model.name);
				row.insert(QStringLiteral("contextWindow"), model.hasContextWindow ? model.contextWindow : 0);
				rows.append(row);
			}
			m_candidates = rows;
			emit candidatesChanged();
			setStatus(rows.isEmpty()
				? QStringLiteral("没有发现可用模型（key 是否正确？）")
				: QStringLiteral("发现 %1 个候选模型，点「添加」写入配置。").arg(rows.size()));
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(QStringLiteral("发现模型失败：%1 %2").arg(error.code, error.message));
		});
}

void SettingsController::addModel(const QString& provider, const QString& modelId, const QString& displayName)
{
	const ConfigurableProvider* entry = findProvider(provider);
	const SettingsNamespace* ns = entry ? findNamespace(*entry) : nullptr;
	if (!m_api || !entry || !ns) {
		setStatus(QStringLiteral("找不到提供方 %1 的设置命名空间。").arg(provider));
		return;
	}
	if (modelId.trimmed().isEmpty()) {
		setStatus(QStringLiteral("模型 id 不能为空。"));
		return;
	}

	AddModelRequest request;
	request.provider = entry->provider;
	request.id = modelId.trimmed();
	request.name = displayName.trimmed();
	// 容量等字段留给"发现"回来的数值（addModel 会按 pi-ai / 非 pi-ai 两种形态写条目）
	for (const DiscoveredModel& model : m_discovered) {
		if (model.id != request.id)
			continue;
		request.hasContextWindow = model.hasContextWindow;
		request.contextWindow = model.contextWindow;
		request.hasMaxTokens = model.hasMaxTokens;
		request.maxTokens = model.maxTokens;
		break;
	}

	setBusy(true);
	ModelSelectionService::addModel(m_api, *entry, *ns, request,
		[this, provider, modelId](const SettingsNamespace&) {
			setBusy(false);
			setStatus(QStringLiteral("已添加模型 %1。").arg(modelId));
			emit modelAdded(provider, modelId);   // 宿主据此刷新输入区的模型选择器（旧壳 DSHHub.043）
			refresh();
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			setStatus(QStringLiteral("添加模型失败：%1 %2").arg(error.code, error.message));
		});
}
