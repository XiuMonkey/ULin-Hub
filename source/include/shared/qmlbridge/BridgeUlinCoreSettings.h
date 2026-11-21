#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

class UlinCoreSettingsBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
	Q_PROPERTY(QString selectedProvider READ selectedProvider NOTIFY changed)
	Q_PROPERTY(QString selectedModel READ selectedModel NOTIFY changed)
	Q_PROPERTY(bool selectedProviderHasKey READ selectedProviderHasKey NOTIFY changed)
	Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY changed)
	Q_PROPERTY(QString workspaceRoot READ workspaceRoot WRITE setWorkspaceRoot NOTIFY changed)
	Q_PROPERTY(int toolThreads READ toolThreads WRITE setToolThreads NOTIFY changed)
	Q_PROPERTY(int maxSteps READ maxSteps WRITE setMaxSteps NOTIFY changed)
	Q_PROPERTY(bool allowWrites READ allowWrites WRITE setAllowWrites NOTIFY changed)
	Q_PROPERTY(QString dataDir READ dataDir NOTIFY changed)
	Q_PROPERTY(QString settingsPath READ settingsPath NOTIFY changed)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
	explicit UlinCoreSettingsBridge(QObject* parent = nullptr);

	QVariantList providers() const { return UlinCore::UlinCoreSettings::instance().providers(); }
	QString selectedProvider() const { return UlinCore::UlinCoreSettings::instance().selectedProvider(); }
	QString selectedModel() const { return UlinCore::UlinCoreSettings::instance().selectedModel(); }
	bool selectedProviderHasKey() const { return UlinCore::UlinCoreSettings::instance().selectedProviderHasKey(); }
	QString systemPrompt() const { return UlinCore::UlinCoreSettings::instance().systemPrompt(); }
	QString workspaceRoot() const { return UlinCore::UlinCoreSettings::instance().workspaceRoot(); }
	int toolThreads() const { return UlinCore::UlinCoreSettings::instance().toolThreads(); }
	int maxSteps() const { return UlinCore::UlinCoreSettings::instance().maxSteps(); }
	bool allowWrites() const { return UlinCore::UlinCoreSettings::instance().allowWrites(); }
	QString dataDir() const { return UlinCore::UlinCoreSettings::instance().dataDir(); }
	QString settingsPath() const { return UlinCore::UlinCoreSettings::instance().settingsPath(); }
	QString status() const { return UlinCore::UlinCoreSettings::instance().status(); }

	void setSystemPrompt(const QString& value) { UlinCore::UlinCoreSettings::instance().setSystemPrompt(value); }
	void setWorkspaceRoot(const QString& value) { UlinCore::UlinCoreSettings::instance().setWorkspaceRoot(value); }
	void setToolThreads(int value) { UlinCore::UlinCoreSettings::instance().setToolThreads(value); }
	void setMaxSteps(int value) { UlinCore::UlinCoreSettings::instance().setMaxSteps(value); }
	void setAllowWrites(bool value) { UlinCore::UlinCoreSettings::instance().setAllowWrites(value); }

	Q_INVOKABLE void addProvider(const QString& name, const QString& baseUrl, const QString& apiKey)
	{
		UlinCore::UlinCoreSettings::instance().addProvider(name, baseUrl, apiKey);
	}
	Q_INVOKABLE void updateProvider(const QString& id, const QString& name,
		const QString& baseUrl, const QString& apiKey)
	{
		UlinCore::UlinCoreSettings::instance().updateProvider(id, name, baseUrl, apiKey);
	}
	Q_INVOKABLE void removeProvider(const QString& id)
	{
		UlinCore::UlinCoreSettings::instance().removeProvider(id);
	}
	Q_INVOKABLE void selectProvider(const QString& id)
	{
		UlinCore::UlinCoreSettings::instance().selectProvider(id);
	}
	Q_INVOKABLE void addModel(const QString& providerId, const QString& id, const QString& name,
		int maxTokens, double temperature)
	{
		UlinCore::UlinCoreSettings::instance().addModel(providerId, id, name, maxTokens, temperature);
	}
	Q_INVOKABLE void updateModel(const QString& providerId, const QString& modelId,
		const QString& name, int maxTokens, double temperature)
	{
		UlinCore::UlinCoreSettings::instance().updateModel(providerId, modelId, name, maxTokens, temperature);
	}
	Q_INVOKABLE void removeModel(const QString& providerId, const QString& modelId)
	{
		UlinCore::UlinCoreSettings::instance().removeModel(providerId, modelId);
	}
	Q_INVOKABLE void selectModel(const QString& providerId, const QString& modelId)
	{
		UlinCore::UlinCoreSettings::instance().selectModel(providerId, modelId);
	}
	Q_INVOKABLE void fetchModels(const QString& providerId)
	{
		UlinCore::UlinCoreSettings::instance().fetchModels(providerId);
	}
	Q_INVOKABLE void save() { UlinCore::UlinCoreSettings::instance().save(); }
	Q_INVOKABLE void reload() { UlinCore::UlinCoreSettings::instance().reload(); }
	Q_INVOKABLE void resetToDefaults() { UlinCore::UlinCoreSettings::instance().resetToDefaults(); }
	Q_INVOKABLE void openDataDir() { UlinCore::UlinCoreSettings::instance().openDataDir(); }

signals:
	void changed();
	void statusChanged();
};