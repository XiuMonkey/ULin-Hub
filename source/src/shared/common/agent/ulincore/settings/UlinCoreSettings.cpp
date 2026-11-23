#include "common/agent/ulincore/settings/UlinCoreSettings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QVariantMap>

namespace UlinCore
{
namespace
{
QString slugFor(const QString& text, const QString& fallback)
{
	QString slug = text.trimmed().toLower();
	slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
	while (slug.startsWith(QLatin1Char('-')))
		slug.remove(0, 1);
	while (slug.endsWith(QLatin1Char('-')))
		slug.chop(1);
	return slug.isEmpty() ? fallback : slug;
}
} // namespace

UlinCoreSettings& UlinCoreSettings::instance()
{
	static UlinCoreSettings settings;
	return settings;
}

UlinCoreSettings::UlinCoreSettings(QObject* parent)
	: QObject(parent)
{
}

void UlinCoreSettings::setDataDir(const QString& dir)
{
	// 目录变化或尚未加载时才重读设置。
	if (m_dataDir == dir && m_loaded)
		return;
	m_dataDir = dir;
	m_loaded = false;
	ensureLoaded();
}

QString UlinCoreSettings::settingsPath() const
{
	const QString dir = m_dataDir.isEmpty()
		? QCoreApplication::applicationDirPath() + QStringLiteral("/data/ulincore")
		: m_dataDir;
	return dir + QStringLiteral("/settings.json");
}

const UlinCoreSettings::ProviderEntry* UlinCoreSettings::providerById(const QString& id) const
{
	for (const ProviderEntry& provider : m_providers) {
		if (provider.id == id)
			return &provider;
	}
	return nullptr;
}

const UlinCoreSettings::ModelEntry* UlinCoreSettings::modelById(const ProviderEntry& provider, const QString& id) const
{
	for (const ModelEntry& model : provider.models) {
		if (model.id == id)
			return &model;
	}
	return nullptr;
}

void UlinCoreSettings::selectFirstAvailable()
{
	// 保证选中的 provider/model 始终有效，失效时回退到第一个。
	if (m_providers.isEmpty()) {
		m_selectedProvider.clear();
		m_selectedModel.clear();
		return;
	}
	if (!providerById(m_selectedProvider))
		m_selectedProvider = m_providers.first().id;

	const ProviderEntry* provider = providerById(m_selectedProvider);
	if (!provider || provider->models.isEmpty()) {
		m_selectedModel.clear();
		return;
	}
	if (!modelById(*provider, m_selectedModel))
		m_selectedModel = provider->models.first().id;
}

void UlinCoreSettings::ensureLoaded()
{
	// 首次访问时读取 settings.json；文件不存在就落默认配置。
	if (m_loaded)
		return;
	m_loaded = true;

	if (m_dataDir.isEmpty())
		m_dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/data/ulincore");
	QDir().mkpath(m_dataDir);

	const QString path = settingsPath();
	if (!QFile::exists(path)) {
		resetToDefaults();
		return;
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

	m_providers.clear();
	for (const QJsonValue& value : root.value(QStringLiteral("providers")).toArray()) {
		const QJsonObject object = value.toObject();
		ProviderEntry provider;
		provider.id = object.value(QStringLiteral("id")).toString().trimmed();
		if (provider.id.isEmpty())
			continue;
		provider.name = object.value(QStringLiteral("name")).toString();
		if (provider.name.isEmpty())
			provider.name = provider.id;
		provider.baseUrl = object.value(QStringLiteral("baseUrl")).toString().trimmed();
		provider.apiKey = object.value(QStringLiteral("apiKey")).toString().trimmed();

		for (const QJsonValue& modelValue : object.value(QStringLiteral("models")).toArray()) {
			const QJsonObject modelObject = modelValue.toObject();
			ModelEntry model;
			model.id = modelObject.value(QStringLiteral("id")).toString().trimmed();
			if (model.id.isEmpty())
				continue;
			model.name = modelObject.value(QStringLiteral("name")).toString();
			if (model.name.isEmpty())
				model.name = model.id;
			model.maxTokens = modelObject.value(QStringLiteral("maxTokens")).toInt(4096);
			model.temperature = modelObject.value(QStringLiteral("temperature")).toDouble(0.2);
			provider.models.append(model);
		}
		m_providers.append(provider);
	}

	// 旧版单 provider 结构迁移：{ model: { baseUrl, apiKey, name, ... } }
	if (m_providers.isEmpty() && root.contains(QStringLiteral("model"))) {
		const QJsonObject legacy = root.value(QStringLiteral("model")).toObject();
		const QString modelId = legacy.value(QStringLiteral("name")).toString().trimmed();
		if (!modelId.isEmpty()) {
			ProviderEntry provider;
			provider.id = QStringLiteral("deepseek");
			provider.name = QStringLiteral("DeepSeek");
			provider.baseUrl = legacy.value(QStringLiteral("baseUrl")).toString().trimmed();
			provider.apiKey = legacy.value(QStringLiteral("apiKey")).toString().trimmed();
			ModelEntry model;
			model.id = modelId;
			model.name = modelId;
			model.maxTokens = legacy.value(QStringLiteral("maxTokens")).toInt(4096);
			model.temperature = legacy.value(QStringLiteral("temperature")).toDouble(0.2);
			provider.models.append(model);
			m_providers.append(provider);
		}
	}

	if (m_providers.isEmpty())
		resetToDefaults();
	else
		selectFirstAvailable();

	m_selectedProvider = root.value(QStringLiteral("selectedProvider")).toString(m_selectedProvider);
	m_selectedModel = root.value(QStringLiteral("selectedModel")).toString(m_selectedModel);
	if (providerById(m_selectedProvider))
		selectFirstAvailable();

	const QString prompt = root.value(QStringLiteral("systemPrompt")).toString();
	if (!prompt.isEmpty())
		m_systemPrompt = prompt;
	const QString workspace = root.value(QStringLiteral("workspaceRoot")).toString().trimmed();
	if (!workspace.isEmpty())
		m_workspaceRoot = workspace;
	m_toolThreads = root.value(QStringLiteral("toolThreads")).toInt(m_toolThreads);
	m_maxSteps = root.value(QStringLiteral("maxSteps")).toInt(m_maxSteps);
	m_allowWrites = root.value(QStringLiteral("allowWrites")).toBool(m_allowWrites);
}

QVariantList UlinCoreSettings::providers() const
{
	// QML 侧按行读取 provider 与 model，selected 只标记当前项。
	QVariantList rows;
	for (const ProviderEntry& provider : m_providers) {
		QVariantList models;
		for (const ModelEntry& model : provider.models) {
			models.append(QVariantMap{
				{ QStringLiteral("id"), model.id },
				{ QStringLiteral("name"), model.name },
				{ QStringLiteral("maxTokens"), model.maxTokens },
				{ QStringLiteral("temperature"), model.temperature }
			});
		}
		rows.append(QVariantMap{
			{ QStringLiteral("id"), provider.id },
			{ QStringLiteral("name"), provider.name },
			{ QStringLiteral("baseUrl"), provider.baseUrl },
			{ QStringLiteral("apiKey"), provider.apiKey },
			{ QStringLiteral("models"), models },
			{ QStringLiteral("selected"), provider.id == m_selectedProvider }
		});
	}
	return rows;
}

QString UlinCoreSettings::selectedBaseUrl() const
{
	const ProviderEntry* provider = providerById(m_selectedProvider);
	return provider ? provider->baseUrl : QString();
}

QString UlinCoreSettings::selectedApiKey() const
{
	const ProviderEntry* provider = providerById(m_selectedProvider);
	return provider ? provider->apiKey : QString();
}

QString UlinCoreSettings::selectedModelName() const
{
	const ProviderEntry* provider = providerById(m_selectedProvider);
	if (!provider)
		return QString();
	const ModelEntry* model = modelById(*provider, m_selectedModel);
	return model ? model->id : QString();
}

// 本地没填 key 时回落到环境变量，供首次启动直接可用。
bool UlinCoreSettings::selectedProviderHasKey() const
{
	if (!selectedApiKey().isEmpty())
		return true;
	return !qEnvironmentVariable("DEEPSEEK_API_KEY").isEmpty()
		|| !qEnvironmentVariable("OPENAI_API_KEY").isEmpty();
}

int UlinCoreSettings::selectedMaxTokens() const
{
	const ProviderEntry* provider = providerById(m_selectedProvider);
	if (!provider)
		return 4096;
	const ModelEntry* model = modelById(*provider, m_selectedModel);
	return model ? model->maxTokens : 4096;
}

double UlinCoreSettings::selectedTemperature() const
{
	const ProviderEntry* provider = providerById(m_selectedProvider);
	if (!provider)
		return 0.2;
	const ModelEntry* model = modelById(*provider, m_selectedModel);
	return model ? model->temperature : 0.2;
}

void UlinCoreSettings::addProvider(const QString& name, const QString& baseUrl, const QString& apiKey)
{
	// provider id 由名字生成 slug，冲突时追加序号。
	ProviderEntry provider;
	provider.id = slugFor(name, QStringLiteral("provider"));
	QString id = provider.id;
	for (int suffix = 2; providerById(id); ++suffix)
		id = provider.id + QStringLiteral("-%1").arg(suffix);
	provider.id = id;
	provider.name = name.trimmed().isEmpty() ? id : name.trimmed();
	provider.baseUrl = baseUrl.trimmed();
	provider.apiKey = apiKey.trimmed();
	m_providers.append(provider);
	m_selectedProvider = provider.id;
	m_selectedModel.clear();
	emit changed();
	save();
}

void UlinCoreSettings::updateProvider(const QString& id, const QString& name, const QString& baseUrl, const QString& apiKey)
{
	for (ProviderEntry& provider : m_providers) {
		if (provider.id != id)
			continue;
		provider.name = name.trimmed().isEmpty() ? provider.id : name.trimmed();
		provider.baseUrl = baseUrl.trimmed();
		provider.apiKey = apiKey.trimmed();
		selectFirstAvailable();
		emit changed();
		save();
		return;
	}
}

void UlinCoreSettings::removeProvider(const QString& id)
{
	for (int i = 0; i < m_providers.size(); ++i) {
		if (m_providers.at(i).id != id)
			continue;
		m_providers.removeAt(i);
		selectFirstAvailable();
		emit changed();
		save();
		return;
	}
}

void UlinCoreSettings::selectProvider(const QString& id)
{
	if (!providerById(id))
		return;
	m_selectedProvider = id;
	m_selectedModel.clear();
	selectFirstAvailable();
	emit changed();
	save();
}

void UlinCoreSettings::addModel(const QString& providerId, const QString& id, const QString& name, int maxTokens, double temperature)
{
	ProviderEntry* provider = nullptr;
	for (ProviderEntry& entry : m_providers) {
		if (entry.id == providerId) {
			provider = &entry;
			break;
		}
	}
	if (!provider)
		return;

	ModelEntry model;
	model.id = slugFor(id, slugFor(name, QStringLiteral("model")));
	if (model.id.isEmpty())
		return;
	QString unique = model.id;
	for (int suffix = 2; modelById(*provider, unique); ++suffix)
		unique = model.id + QStringLiteral("-%1").arg(suffix);
	model.id = unique;
	model.name = name.trimmed().isEmpty() ? model.id : name.trimmed();
	// 模型参数写入前做范围夹取。
	model.maxTokens = qBound(128, maxTokens, 65536);
	model.temperature = temperature;
	provider->models.append(model);

	if (m_selectedProvider == providerId && m_selectedModel.isEmpty())
		m_selectedModel = model.id;
	emit changed();
	save();
}

void UlinCoreSettings::updateModel(const QString& providerId, const QString& modelId, const QString& name, int maxTokens, double temperature)
{
	for (ProviderEntry& provider : m_providers) {
		if (provider.id != providerId)
			continue;
		for (ModelEntry& model : provider.models) {
			if (model.id != modelId)
				continue;
			model.name = name.trimmed().isEmpty() ? model.id : name.trimmed();
			model.maxTokens = qBound(128, maxTokens, 65536);
			model.temperature = temperature;
			emit changed();
			save();
			return;
		}
	}
}

void UlinCoreSettings::removeModel(const QString& providerId, const QString& modelId)
{
	for (ProviderEntry& provider : m_providers) {
		if (provider.id != providerId)
			continue;
		for (int i = 0; i < provider.models.size(); ++i) {
			if (provider.models.at(i).id != modelId)
				continue;
			provider.models.removeAt(i);
			if (m_selectedProvider == providerId && m_selectedModel == modelId) {
				m_selectedModel.clear();
				selectFirstAvailable();
			}
			emit changed();
			save();
			return;
		}
	}
}

void UlinCoreSettings::selectModel(const QString& providerId, const QString& modelId)
{
	const ProviderEntry* provider = providerById(providerId);
	if (!provider || !modelById(*provider, modelId))
		return;
	m_selectedProvider = providerId;
	m_selectedModel = modelId;
	emit changed();
	save();
}

void UlinCoreSettings::setStatus(const QString& status)
{
	m_status = status;
	emit statusChanged();
}

// GET <base>/models 拉取模型清单：只补新增项，不覆盖本地已配置的名称与参数。
void UlinCoreSettings::fetchModels(const QString& providerId)
{
	const ProviderEntry* provider = providerById(providerId);
	if (!provider) {
		setStatus(QStringLiteral("提供商不存在"));
		return;
	}

	QString base = provider->baseUrl.trimmed();
	while (base.endsWith(QLatin1Char('/')))
		base.chop(1);
	if (base.endsWith(QStringLiteral("/chat/completions")))
		base.chop(QStringLiteral("/chat/completions").size());
	if (base.isEmpty()) {
		setStatus(QStringLiteral("请先填写接口地址"));
		return;
	}

	QString key = provider->apiKey;
	if (key.isEmpty())
		key = qEnvironmentVariable("DEEPSEEK_API_KEY");
	if (key.isEmpty())
		key = qEnvironmentVariable("OPENAI_API_KEY");
	if (key.isEmpty()) {
		setStatus(QStringLiteral("请先填写 API Key"));
		return;
	}

	if (!m_network)
		m_network = new QNetworkAccessManager(this);

	// 模型列表端点由 baseUrl 推导，兼容填了 /chat/completions 的旧配置。
	QNetworkRequest request(QUrl(base + QStringLiteral("/models")));
	request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
	request.setRawHeader("Accept", "application/json");

	setStatus(QStringLiteral("正在获取模型…"));
	QNetworkReply* reply = m_network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, providerId]() {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			setStatus(QStringLiteral("获取失败：") + reply->errorString());
			return;
		}

		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		QJsonArray data = root.value(QStringLiteral("data")).toArray();
		if (data.isEmpty())
			data = root.value(QStringLiteral("models")).toArray();

		ProviderEntry* target = nullptr;
		for (ProviderEntry& entry : m_providers) {
			if (entry.id == providerId) {
				target = &entry;
				break;
			}
		}
		if (!target) {
			setStatus(QStringLiteral("提供商已被删除"));
			return;
		}
		if (data.isEmpty()) {
			setStatus(QStringLiteral("提供商没有返回模型"));
			return;
		}

		int added = 0;
		for (const QJsonValue& value : data) {
			const QJsonObject object = value.toObject();
			QString id = object.value(QStringLiteral("id")).toString().trimmed();
			if (id.isEmpty())
				id = object.value(QStringLiteral("name")).toString().trimmed();
			if (id.isEmpty() || modelById(*target, id))
				continue;
			ModelEntry model;
			model.id = id;
			model.name = id;
			target->models.append(model);
			++added;
		}

		selectFirstAvailable();
		emit changed();
		save();
		setStatus(added > 0
			? QStringLiteral("已获取 %1 个模型（新增 %2）").arg(data.size()).arg(added)
			: QStringLiteral("已获取 %1 个模型（无新增）").arg(data.size()));
	});
}

bool UlinCoreSettings::writeFile(QString* error) const
{
	const QString path = settingsPath();
	QDir().mkpath(QFileInfo(path).absolutePath());

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		if (error)
			*error = QStringLiteral("无法写入 ") + path;
		return false;
	}

	// 统一写 settings.json：providers、当前选中项与全局选项。
	QJsonArray providers;
	for (const ProviderEntry& provider : m_providers) {
		QJsonArray models;
		for (const ModelEntry& model : provider.models) {
			models.append(QJsonObject{
				{ QStringLiteral("id"), model.id },
				{ QStringLiteral("name"), model.name },
				{ QStringLiteral("maxTokens"), model.maxTokens },
				{ QStringLiteral("temperature"), model.temperature }
			});
		}
		providers.append(QJsonObject{
			{ QStringLiteral("id"), provider.id },
			{ QStringLiteral("name"), provider.name },
			{ QStringLiteral("baseUrl"), provider.baseUrl },
			{ QStringLiteral("apiKey"), provider.apiKey },
			{ QStringLiteral("models"), models }
		});
	}

	const QJsonObject body{
		{ QStringLiteral("providers"), providers },
		{ QStringLiteral("selectedProvider"), m_selectedProvider },
		{ QStringLiteral("selectedModel"), m_selectedModel },
		{ QStringLiteral("systemPrompt"), m_systemPrompt },
		{ QStringLiteral("workspaceRoot"), m_workspaceRoot },
		{ QStringLiteral("toolThreads"), m_toolThreads },
		{ QStringLiteral("maxSteps"), m_maxSteps },
		{ QStringLiteral("allowWrites"), m_allowWrites }
	};
	file.write(QJsonDocument(body).toJson(QJsonDocument::Indented));

	if (!file.commit()) {
		if (error)
			*error = QStringLiteral("写入失败：") + path;
		return false;
	}
	return true;
}

void UlinCoreSettings::save()
{
	// 保存成功后发 saved 信号，主代理据此重建模型。
	QString error;
	if (!writeFile(&error)) {
		setStatus(error);
		return;
	}
	setStatus(QStringLiteral("已保存，下一轮生效"));
	emit saved();
}

void UlinCoreSettings::reload()
{
	m_loaded = false;
	ensureLoaded();
	emit changed();
}

void UlinCoreSettings::resetToDefaults()
{
	m_providers.clear();

	// 默认配置：DeepSeek 两个模型、4 线程、24 步、默认禁写。
	ProviderEntry provider;
	provider.id = QStringLiteral("deepseek");
	provider.name = QStringLiteral("DeepSeek");
	provider.baseUrl = QStringLiteral("https://api.deepseek.com/v1");

	ModelEntry chat;
	chat.id = QStringLiteral("deepseek-chat");
	chat.name = QStringLiteral("DeepSeek Chat");
	chat.maxTokens = 4096;
	chat.temperature = 0.2;
	provider.models.append(chat);

	ModelEntry reasoner;
	reasoner.id = QStringLiteral("deepseek-reasoner");
	reasoner.name = QStringLiteral("DeepSeek Reasoner");
	reasoner.maxTokens = 8192;
	reasoner.temperature = 0.2;
	provider.models.append(reasoner);

	m_providers.append(provider);
	m_selectedProvider = provider.id;
	m_selectedModel = chat.id;
	m_systemPrompt = QStringLiteral("You are ULin Core, a concise coding agent. Use tools when they help.");
	m_workspaceRoot = QStringLiteral("launch-root");
	m_toolThreads = 4;
	m_maxSteps = 24;
	m_allowWrites = false;
	m_loaded = true;
	save();
	emit changed();
}

void UlinCoreSettings::openDataDir()
{
	if (m_dataDir.isEmpty())
		m_dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/data/ulincore");
	QDir().mkpath(m_dataDir);
	QDesktopServices::openUrl(QUrl::fromLocalFile(m_dataDir));
}

void UlinCoreSettings::setSystemPrompt(const QString& value)
{
	if (m_systemPrompt == value)
		return;
	m_systemPrompt = value;
	emit changed();
}

void UlinCoreSettings::setWorkspaceRoot(const QString& value)
{
	const QString trimmed = value.trimmed();
	if (m_workspaceRoot == trimmed)
		return;
	m_workspaceRoot = trimmed;
	emit changed();
}

void UlinCoreSettings::setToolThreads(int value)
{
	const int clamped = qBound(1, value, 16);
	if (m_toolThreads == clamped)
		return;
	m_toolThreads = clamped;
	emit changed();
}

void UlinCoreSettings::setMaxSteps(int value)
{
	const int clamped = qBound(1, value, 64);
	if (m_maxSteps == clamped)
		return;
	m_maxSteps = clamped;
	emit changed();
}

void UlinCoreSettings::setAllowWrites(bool value)
{
	if (m_allowWrites == value)
		return;
	m_allowWrites = value;
	emit changed();
}
} // namespace UlinCore
