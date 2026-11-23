#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"
#include "qmlbridge/BridgeUlinCoreSettings.h"

class SettingsBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(bool writable READ writable NOTIFY viewChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(QVariantList providers READ providers NOTIFY viewChanged)
	Q_PROPERTY(QVariantList models READ models NOTIFY viewChanged)
	Q_PROPERTY(QVariantList failures READ failures NOTIFY viewChanged)
	Q_PROPERTY(int groupCount READ groupCount NOTIFY viewChanged)
	Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
	Q_PROPERTY(QString formFeedback READ formFeedback NOTIFY formFeedbackChanged)
	Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
	Q_PROPERTY(int page READ page NOTIFY pageChanged)
	Q_PROPERTY(QVariantList agentPresets READ agentPresets NOTIFY presetsChanged)
	Q_PROPERTY(QString defaultPresetName READ defaultPresetName NOTIFY presetsChanged)
	Q_PROPERTY(QString presetFeedback READ presetFeedback NOTIFY presetsChanged)
	Q_PROPERTY(QString masterBackend READ masterBackend NOTIFY masterBackendChanged)
	Q_PROPERTY(bool ulincoreMode READ ulincoreMode NOTIFY masterBackendChanged)
	Q_PROPERTY(bool switchingBackend READ switchingBackend NOTIFY switchingBackendChanged)
	Q_PROPERTY(QString masterBackendName READ masterBackendName NOTIFY masterBackendChanged)
	Q_PROPERTY(QVariantList masterBackends READ masterBackends NOTIFY masterBackendChanged)
	Q_PROPERTY(UlinCoreSettingsBridge* ulincoreBridge READ ulincoreBridge CONSTANT)
	Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)
	Q_PROPERTY(QVariantList languages READ languages NOTIFY languageChanged)
	Q_PROPERTY(QString languageCode READ languageCode NOTIFY languageChanged)
	Q_PROPERTY(QString appearanceFeedback READ appearanceFeedback NOTIFY languageChanged)
	Q_PROPERTY(QVariantMap account READ account NOTIFY accountChanged)
	Q_PROPERTY(bool accountOpen READ accountOpen NOTIFY accountOpenChanged)
	Q_PROPERTY(bool accountLoginHint READ accountLoginHint NOTIFY accountLoginHintChanged)

public:
	explicit SettingsBridge(SettingsController* controller, QObject* parent = nullptr);

	bool isOpen() const { return m_controller->isOpen(); }
	bool busy() const { return m_controller->busy(); }
	bool writable() const { return m_controller->writable(); }
	QString status() const { return m_controller->status(); }
	QVariantList providers() const { return m_controller->providers(); }
	QVariantList models() const { return m_controller->models(); }
	QVariantList failures() const { return m_controller->failures(); }
	int groupCount() const { return m_controller->groupCount(); }
	QString notice() const { return m_controller->notice(); }
	QString formFeedback() const { return m_controller->formFeedback(); }
	QVariantList candidates() const { return m_controller->candidates(); }
	int page() const { return m_controller->page(); }
	QVariantList agentPresets() const { return m_controller->agentPresets(); }
	QString defaultPresetName() const { return m_controller->defaultPresetName(); }
	QString presetFeedback() const { return m_controller->presetFeedback(); }
	QString masterBackend() const { return m_controller->masterBackend(); }
	bool ulincoreMode() const { return m_controller->masterBackend() == QLatin1String("ulincore"); }
	bool switchingBackend() const { return m_controller->switchingBackend(); }
	QString masterBackendName() const { return m_controller->masterBackendName(); }
	QVariantList masterBackends() const { return m_controller->masterBackends(); }
	UlinCoreSettingsBridge* ulincoreBridge() const { return m_ulincoreBridge; }
	QString serverUrl() const { return m_controller->serverUrl(); }
	QVariantList languages() const { return m_controller->languages(); }
	QString languageCode() const { return m_controller->languageCode(); }
	QString appearanceFeedback() const { return m_controller->appearanceFeedback(); }
	QVariantMap account() const { return m_controller->account(); }
	bool accountOpen() const { return m_controller->accountOpen(); }
	bool accountLoginHint() const { return m_controller->accountLoginHint(); }

	Q_INVOKABLE void openPage() { m_controller->openPage(); }
	Q_INVOKABLE void closePage() { m_controller->closePage(); }
	Q_INVOKABLE void setPage(int page) { m_controller->setPage(page); }
	Q_INVOKABLE void refresh() { m_controller->refresh(); }
	Q_INVOKABLE void saveApiKey(const QString& provider, const QString& apiKey)
	{
		m_controller->saveApiKey(provider, apiKey);
	}
	Q_INVOKABLE void discover(const QString& provider, const QString& apiKey)
	{
		m_controller->discover(provider, apiKey);
	}
	Q_INVOKABLE void addModel(const QString& provider, const QString& modelId, const QString& displayName)
	{
		m_controller->addModel(provider, modelId, displayName);
	}
	Q_INVOKABLE void createCustomProvider(const QString& route, const QString& displayName, const QString& baseUrl, const QString& api, const QString& modelId, const QString& modelName, const QString& apiKey)
	{
		m_controller->createCustomProvider(route, displayName, baseUrl, api, modelId, modelName, apiKey);
	}
	Q_INVOKABLE void submitModelForm(const QString& provider, const QString& modelId, const QString& displayName, const QString& contextWindow, const QString& maxTokens, const QString& efforts, const QString& apiKey)
	{
		m_controller->submitModelForm(provider, modelId, displayName, contextWindow, maxTokens, efforts, apiKey);
	}
	Q_INVOKABLE QString routeHint(const QString& provider) const
	{
		return m_controller->routeHint(provider);
	}
	Q_INVOKABLE bool routeIsPiAi(const QString& provider) const
	{
		return m_controller->routeIsPiAi(provider);
	}
	// UI 语义：该 provider 的模型表单是否显示思考档位（当前与 pi-ai 路由一致）。
	Q_INVOKABLE bool providerSupportsThinkingLevels(const QString& provider) const
	{
		return m_controller->routeIsPiAi(provider);
	}
	Q_INVOKABLE void removeModel(const QString& provider, const QString& modelId)
	{
		m_controller->removeModel(provider, modelId);
	}
	Q_INVOKABLE void selectDefaultPreset(const QString& presetId)
	{
		m_controller->selectDefaultPreset(presetId);
	}
	Q_INVOKABLE void selectMasterBackend(const QString& backendId)
	{
		m_controller->selectMasterBackend(backendId);
	}
	Q_INVOKABLE void refreshMasterBackends() { m_controller->refreshMasterBackends(); }
	Q_INVOKABLE void saveServerUrl(const QString& url) { m_controller->saveServerUrl(url); }
	Q_INVOKABLE void setLanguage(const QString& code) { m_controller->setLanguage(code); }
	Q_INVOKABLE void resetStyles() { m_controller->resetStyles(); }
	Q_INVOKABLE void openAccountPage() { m_controller->openAccountPage(); }
	Q_INVOKABLE void dismissAccountLoginHint() { m_controller->dismissAccountLoginHint(); }
	Q_INVOKABLE void closeAccountPage() { m_controller->closeAccountPage(); }
	Q_INVOKABLE void refreshAccount() { m_controller->refreshAccount(); }
	Q_INVOKABLE void startAccountSignIn() { m_controller->startAccountSignIn(); }
	Q_INVOKABLE void cancelAccountSignIn() { m_controller->cancelAccountSignIn(); }
	Q_INVOKABLE void signOutAccount() { m_controller->signOutAccount(); }
	Q_INVOKABLE void openAccountUsage() { m_controller->openAccountUsage(); }
	Q_INVOKABLE void openAccountTopUp() { m_controller->openAccountTopUp(); }
	Q_INVOKABLE void openAccountAuthorize() { m_controller->openAccountAuthorize(); }

signals:
	void openChanged();
	void busyChanged();
	void viewChanged();
	void statusChanged();
	void noticeChanged();
	void formFeedbackChanged();
	void candidatesChanged();
	void pageChanged();
	void presetsChanged();
	void masterBackendChanged();
	void switchingBackendChanged();
	void serverUrlChanged();
	void languageChanged();
	void accountChanged();
	void accountOpenChanged();
	void accountLoginHintChanged();

private:
	SettingsController* m_controller = nullptr;
	UlinCoreSettingsBridge* m_ulincoreBridge = nullptr;
};