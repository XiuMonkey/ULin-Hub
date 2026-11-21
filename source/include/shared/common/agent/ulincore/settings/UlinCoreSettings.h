#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;

namespace UlinCore
{
// data/ulincore/settings.json 的读写门面：设置页和 Master Agent 共用同一份。
// 结构：providers[]（每个 provider 带 models[]）+ 选中的 provider/model + 运行时设置。
class UlinCoreSettings : public QObject
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
	static UlinCoreSettings& instance();

	void setDataDir(const QString& dir);
	QString dataDir() const { return m_dataDir; }
	QString settingsPath() const;

	QVariantList providers() const;
	QString selectedProvider() const { return m_selectedProvider; }
	QString selectedModel() const { return m_selectedModel; }
	bool selectedProviderHasKey() const;

	QString selectedBaseUrl() const;
	QString selectedApiKey() const;
	QString selectedModelName() const;
	int selectedMaxTokens() const;
	double selectedTemperature() const;

	QString systemPrompt() const { return m_systemPrompt; }
	QString workspaceRoot() const { return m_workspaceRoot; }
	int toolThreads() const { return m_toolThreads; }
	int maxSteps() const { return m_maxSteps; }
	bool allowWrites() const { return m_allowWrites; }
	QString status() const { return m_status; }

	void setSystemPrompt(const QString& value);
	void setWorkspaceRoot(const QString& value);
	void setToolThreads(int value);
	void setMaxSteps(int value);
	void setAllowWrites(bool value);

	void addProvider(const QString& name, const QString& baseUrl, const QString& apiKey);
	void updateProvider(const QString& id, const QString& name,
		const QString& baseUrl, const QString& apiKey);
	void removeProvider(const QString& id);
	void selectProvider(const QString& id);

	void addModel(const QString& providerId, const QString& id, const QString& name,
		int maxTokens, double temperature);
	void updateModel(const QString& providerId, const QString& modelId, const QString& name,
		int maxTokens, double temperature);
	void removeModel(const QString& providerId, const QString& modelId);
	void selectModel(const QString& providerId, const QString& modelId);
	void fetchModels(const QString& providerId);

	void save();
	void reload();
	void resetToDefaults();
	void openDataDir();

signals:
	void changed();
	void saved();
	void statusChanged();

private:
	struct ModelEntry
	{
		QString id;
		QString name;
		int maxTokens = 4096;
		double temperature = 0.2;
	};

	struct ProviderEntry
	{
		QString id;
		QString name;
		QString baseUrl;
		QString apiKey;
		QList<ModelEntry> models;
	};

	explicit UlinCoreSettings(QObject* parent = nullptr);

	void ensureLoaded();
	bool writeFile(QString* error) const;
	const ProviderEntry* providerById(const QString& id) const;
	const ModelEntry* modelById(const ProviderEntry& provider, const QString& id) const;
	void selectFirstAvailable();
	void setStatus(const QString& status);

	QString m_dataDir;
	QList<ProviderEntry> m_providers;
	QString m_selectedProvider;
	QString m_selectedModel;
	QString m_systemPrompt = QStringLiteral(
		"You are ULin Core, a concise coding agent. Use tools when they help.");
	QString m_workspaceRoot = QStringLiteral("launch-root");
	int m_toolThreads = 4;
	int m_maxSteps = 24;
	bool m_allowWrites = false;
	QString m_status;
	bool m_loaded = false;
	QNetworkAccessManager* m_network = nullptr;
};
} // namespace UlinCore
