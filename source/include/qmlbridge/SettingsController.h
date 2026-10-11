#pragma once

// 设置页数据面（M6 前置）：把 ModelSelectionService 的既有能力接到 QML。
//
// 这一页要解决的就是"发消息没有任何输出（MISSING_CREDENTIAL）"：模型与 API key 都由 harness 的
// **凭据服务**持有，客户端不自己存 key（见 ModelSelectionService.h 的注释）。所以流程是：
//   1) fetchView()          -> 可配置的 provider 清单 + settings 命名空间（settingsNs/settingsPath）
//   2) describeCredential() -> 每个 provider 的 key 引用（profile 声明优先，否则按 provider 派生，
//                              如 deepseek-official -> DEEPSEEK_API_KEY）配没配、可不可写、来源
//   3) saveCredential()     -> 写 key（credentials/set）
//   4) discoverModels()     -> 用刚填的 key 拉候选模型
//   5) addModel()           -> 把选中的模型写进该命名空间的 models 数组（settings/mutate）
//
// 逻辑一行没重写：解析、派生引用、读-改-写 models 数组全在既有 ModelSelectionService 里。
// 本类只负责"持有视图状态 + 暴露给 QML + 把回调翻成信号"。

#include "common/session/ModelSelectionService.h"

#include <QObject>
#include <QString>
#include <QVariantList>

class DshApiClient;
class QTimer;

class SettingsController : public QObject
{
	Q_OBJECT
		Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
		Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
		Q_PROPERTY(bool writable READ writable NOTIFY viewChanged)
		Q_PROPERTY(QString status READ status NOTIFY statusChanged)
		Q_PROPERTY(QVariantList providers READ providers NOTIFY viewChanged)
		Q_PROPERTY(QVariantList models READ models NOTIFY viewChanged)
		// 目录读取失败的提供方（原版 modelListStatus 会把它们逐个列出来）
		Q_PROPERTY(QVariantList failures READ failures NOTIFY viewChanged)
		Q_PROPERTY(int groupCount READ groupCount NOTIFY viewChanged)
		// 列表级瞬时提示（原版 modelListNotice，4 秒后自隐）
		Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
		// 表单反馈行（与列表状态分行，旧壳 m_feedback / m_status 是两处）
		Q_PROPERTY(QString formFeedback READ formFeedback NOTIFY formFeedbackChanged)
		Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
		// 导航页下标：0=模型 1=Agent 预设 2=服务端 3=外观（与旧壳 Settings 的导航顺序一致）
		Q_PROPERTY(int page READ page NOTIFY pageChanged)
		// Agent 预设
		Q_PROPERTY(QVariantList agentPresets READ agentPresets NOTIFY presetsChanged)
		Q_PROPERTY(QString defaultPresetName READ defaultPresetName NOTIFY presetsChanged)
		Q_PROPERTY(QString presetFeedback READ presetFeedback NOTIFY presetsChanged)
		// 服务端地址
		Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
		// 外观
		// 注意：这里**不能**是 CONSTANT。openPage() 是先 emit openChanged()（QML 的 Loader 是
		// 同步装载，ComboBox 当场绑定 languages）再 refresh() → loadAppearance() 填数据；
		// 标成 CONSTANT 就等于"绑定时为空、之后永远不再读"，语言下拉会一直空白（无任何报错）。
		Q_PROPERTY(QVariantList languages READ languages NOTIFY languageChanged)
		Q_PROPERTY(QString languageCode READ languageCode NOTIFY languageChanged)
		Q_PROPERTY(QString appearanceFeedback READ appearanceFeedback NOTIFY languageChanged)

public:
	explicit SettingsController(DshApiClient* api, QObject* parent = nullptr);

	bool isOpen() const { return m_open; }
	bool busy() const { return m_busy; }
	bool writable() const { return m_view.settingsWritable; }
	QString status() const { return m_status; }
	QVariantList providers() const { return m_providers; }
	QVariantList models() const { return m_models; }
	QVariantList failures() const { return m_failures; }
	int groupCount() const { return m_groupCount; }
	QString notice() const { return m_notice; }
	QVariantList candidates() const { return m_candidates; }
	int page() const { return m_page; }
	QVariantList agentPresets() const { return m_agentPresets; }
	QString defaultPresetName() const { return m_defaultPresetName; }
	QString presetFeedback() const { return m_presetFeedback; }
	QString serverUrl() const { return m_serverUrl; }
	QVariantList languages() const { return m_languages; }
	QString languageCode() const { return m_languageCode; }
	QString appearanceFeedback() const { return m_appearanceFeedback; }
	QString formFeedback() const { return m_formFeedback; }

	Q_INVOKABLE void openPage();
	Q_INVOKABLE void closePage();
	Q_INVOKABLE void setPage(int page);
	// 重新拉 provider 清单与凭据状态
	Q_INVOKABLE void refresh();
	// 保存 API key（credentials/set）；provider 为路由 id
	Q_INVOKABLE void saveApiKey(const QString& provider, const QString& apiKey);
	// 用当前 key（可空 = 用服务端已存/环境里的）拉候选模型
	Q_INVOKABLE void discover(const QString& provider, const QString& apiKey);
	// 把候选模型加进该 provider 的 models 列表
	Q_INVOKABLE void addModel(const QString& provider, const QString& modelId, const QString& displayName);
	// 原版"新增模型"表单的一次提交（校验 → 可选写凭据 → 整份写回模型），文案用原版 qtTrId
	Q_INVOKABLE void submitModelForm(const QString& provider, const QString& modelId, const QString& displayName, const QString& contextWindow, const QString& maxTokens, const QString& efforts, const QString& apiKey);
	// 原版 routeHint：写回地址 + 覆盖范围提醒 + 只读提醒（ModelListPanel::routeHint）
	Q_INVOKABLE QString routeHint(const QString& provider) const;
	// 该路由是否 pi-ai（决定"思考档位"还是"输入模态"一行显示，与旧壳一致）
	Q_INVOKABLE bool routeIsPiAi(const QString& provider) const;
	// 删除一个模型条目（settings/mutate 里整份写回）
	Q_INVOKABLE void removeModel(const QString& provider, const QString& modelId);
	// Agent 预设：设为默认（settings/update）
	Q_INVOKABLE void selectDefaultPreset(const QString& presetId);
	// 服务端地址：写入本地 SettingsStore 后请宿主重启内置服务端（与旧壳 ULinHub.042 一致）
	Q_INVOKABLE void saveServerUrl(const QString& url);
	// 外观：切语言（立即生效，无需重启）与重置风格包
	Q_INVOKABLE void setLanguage(const QString& code);
	Q_INVOKABLE void resetStyles();

signals:
	void openChanged();
	void busyChanged();
	void viewChanged();
	void statusChanged();
	void candidatesChanged();
	void pageChanged();
	void presetsChanged();
	void serverUrlChanged();
	void languageChanged();
	void formFeedbackChanged();
	void noticeChanged();
	// 宿主据此重启内置服务端（旧壳：Settings::serverSettingsSaved -> ServerManager::restart）
	void serverRestartRequested();
	// 新增模型后宿主可以刷新输入区的模型选择器（M5b 用；旧壳 ULinHub.043）
	void modelAdded(const QString& provider, const QString& modelId);

private:
	const ConfigurableProvider* findProvider(const QString& provider) const;
	const SettingsNamespace* findNamespace(const ConfigurableProvider& provider) const;
	void setBusy(bool busy);
	void setStatus(const QString& text);
	void setFormFeedback(const QString& text);
	void setNotice(const QString& text);
	void rebuildProviders();
	void writeModel(const ConfigurableProvider& provider, const SettingsNamespace& ns, const AddModelRequest& request, bool replacing);
	void loadAgentPresets();
	void loadAppearance();

	DshApiClient* m_api = nullptr;
	ServerModelView m_view;
	QVector<CredentialStatus> m_credentials;
	QVector<DiscoveredModel> m_discovered;
	QVariantList m_providers;
	QVariantList m_models;
	QVariantList m_failures;
	QVariantList m_candidates;
	int m_groupCount = 0;
	QString m_notice;
	QTimer* m_noticeTimer = nullptr;
	QVariantList m_agentPresets;
	QVariantList m_languages;
	QString m_status;
	QString m_formFeedback;
	QString m_defaultPresetName;
	QString m_presetFeedback;
	QString m_serverUrl;
	QString m_languageCode;
	QString m_appearanceFeedback;
	int m_page = 0;
	bool m_open = false;
	bool m_busy = false;
};
