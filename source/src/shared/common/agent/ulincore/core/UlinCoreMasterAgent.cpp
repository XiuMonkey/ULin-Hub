#include "common/agent/ulincore/core/UlinCoreMasterAgent.h"

#include "common/agent/ulincore/model/UlinCoreEchoModel.h"
#include "common/agent/ulincore/model/UlinCoreOpenAiModel.h"
#include "common/agent/ulincore/settings/UlinCoreSettings.h"

#include "VirtualClass/VirtualMain.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace UlinCore
{
UlinCoreMasterAgent::UlinCoreMasterAgent(VirtualApiHost* host, VirtualMain* main, QObject* parent)
	: QObject(parent)
	, m_host(host)
	, m_main(main)
{
}

UlinCoreMasterAgent::~UlinCoreMasterAgent() = default;

bool UlinCoreMasterAgent::RegisterExecutor(const char* id, UlinCoreNativeExecutor* executor)
{
	if (!id || !*id || !executor)
		return false;
	const QString executorId = QString::fromUtf8(id);
	// 内置 ID 不允许被扩展覆盖。
	if (executorId.startsWith(QStringLiteral("builtin.")))
		return false;
	ensureKernel();
	if (m_kernel->executors().contains(executorId))
		return false;
	m_kernel->executors().add(executorId,
		std::shared_ptr<UlinCoreNativeExecutor>(executor));
	return true;
}

void UlinCoreMasterAgent::UnregisterExecutor(const char* id)
{
	if (!id || !*id || !m_kernel)
		return;
	m_kernel->executors().remove(QString::fromUtf8(id));
}

bool UlinCoreMasterAgent::HasExecutor(const char* id) const
{
	if (!id || !*id)
		return false;
	if (!m_kernel)
		const_cast<UlinCoreMasterAgent*>(this)->ensureKernel();
	return m_kernel && m_kernel->executors().contains(QString::fromUtf8(id));
}

void UlinCoreMasterAgent::setModel(std::shared_ptr<UlinCoreModel> model)
{
	m_modelExplicit = true;
	m_model = std::move(model);
	if (m_kernel)
		m_kernel->setModel(m_model);
}

void UlinCoreMasterAgent::setWorkspaceRoot(const QString& root)
{
	m_workspaceRoot = root;
}

void UlinCoreMasterAgent::setDataDir(const QString& dir)
{
	m_dataDir = dir;
	if (!m_dataDir.isEmpty()) {
		// dataDir 决定设置文件、会话索引与默认工作区的位置。
		QDir().mkpath(m_dataDir);
		QDir().mkpath(m_dataDir + QStringLiteral("/sessions"));
		if (m_workspaceRoot.isEmpty())
			m_workspaceRoot = m_dataDir + QStringLiteral("/launch-root");
		QDir().mkpath(m_workspaceRoot);
	}
	auto& settings = UlinCoreSettings::instance();
	settings.setDataDir(m_dataDir);
	if (m_kernel && !m_dataDir.isEmpty())
		m_kernel->setIntentsPath(m_dataDir + QStringLiteral("/intents.json"));
	if (!m_settingsConnected) {
		connect(&settings, &UlinCoreSettings::saved, this, &UlinCoreMasterAgent::reloadSettings);
		m_settingsConnected = true;
	}
	loadSettings();
	loadState();
}

void UlinCoreMasterAgent::loadSettings()
{
	if (m_dataDir.isEmpty())
		return;

	auto& settings = UlinCoreSettings::instance();
	settings.setDataDir(m_dataDir);

	m_systemPrompt = settings.systemPrompt();
	const QString workspace = settings.workspaceRoot().trimmed();
	if (!workspace.isEmpty()) {
		m_workspaceRoot = QDir::isAbsolutePath(workspace)
			? workspace
			: QDir(m_dataDir).absoluteFilePath(workspace);
		QDir().mkpath(m_workspaceRoot);
	}

	if (m_modelExplicit)
		return;

	// 未显式注入模型时按设置构建 OpenAI 兼容模型；缺模型名或 key 就退回 echo。
	m_model.reset();
	OpenAiModelConfig config;
	config.baseUrl = settings.selectedBaseUrl().trimmed();
	if (config.baseUrl.isEmpty())
		config.baseUrl = QStringLiteral("https://api.deepseek.com/v1");
	config.apiKey = settings.selectedApiKey();
	if (config.apiKey.isEmpty())
		config.apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY");
	if (config.apiKey.isEmpty())
		config.apiKey = qEnvironmentVariable("OPENAI_API_KEY");
	config.model = settings.selectedModelName().trimmed();
	config.temperature = settings.selectedTemperature();
	config.maxTokens = settings.selectedMaxTokens();

	if (config.model.isEmpty()) {
		qWarning().noquote() << "[UlinCore] no model selected in" << settings.settingsPath()
			<< "-> using echo model";
		return;
	}
	if (config.apiKey.isEmpty()) {
		qWarning().noquote() << "[UlinCore] no API key in" << settings.settingsPath()
			<< "-> using echo model (fill provider apiKey or set DEEPSEEK_API_KEY)";
		return;
	}

	m_model = std::make_shared<UlinCoreOpenAiModel>(config);
	qInfo().noquote() << "[UlinCore] model ready:" << config.model << config.baseUrl;
}

void UlinCoreMasterAgent::reloadSettings()
{
	// 设置变更：取消在跑的回合，重建模型与 Options。
	if (m_kernel && m_kernel->running())
		m_kernel->cancel();

	loadSettings();
	if (m_kernel) {
		m_kernel->setModel(m_model ? m_model : std::make_shared<UlinCoreEchoModel>());
		UlinCoreKernel::Options options = m_kernel->options();
		options.systemPrompt = m_systemPrompt.isEmpty()
			? QStringLiteral("You are ULin Core, a concise coding agent. Use tools when they help.")
			: m_systemPrompt;
		options.toolContext.workspaceRoot = m_workspaceRoot.isEmpty()
			? QDir::currentPath()
			: m_workspaceRoot;
		const auto& settings = UlinCoreSettings::instance();
		options.toolThreads = settings.toolThreads();
		options.maxSteps = settings.maxSteps();
		options.toolContext.allowWrites = settings.allowWrites();
		m_kernel->setOptions(options);
	}
	qInfo().noquote() << "[UlinCore] settings reloaded";
}

void UlinCoreMasterAgent::ActivateMasterAgent()
{
	ensureKernel();
	m_active = true;

	if (m_host)
		m_host->Takenover(true);
	// 接管态不会由 DSH 的 $events ready 触发 connected，后端要自己喊一声"我准备好了"。
	// HandleConnected 内部有一次性闸门，不会滚雪球。
	if (m_main)
		m_main->HandleConnected();

	qInfo().noquote() << "[UlinCore] master agent activated";
}

void UlinCoreMasterAgent::DeactivateMasterAgent()
{
	if (m_kernel)
		m_kernel->cancel();
	m_active = false;
	if (m_host)
		m_host->Takenover(false);
	qInfo().noquote() << "[UlinCore] master agent deactivated";
}

void UlinCoreMasterAgent::ensureKernel()
{
	if (m_kernel)
		return;

	m_kernel = new UlinCoreKernel(this);
	m_kernel->setModel(m_model ? m_model : std::make_shared<UlinCoreEchoModel>());
	if (!m_dataDir.isEmpty())
		m_kernel->setIntentsPath(m_dataDir + QStringLiteral("/intents.json"));

	// 内核懒创建：设置里的线程数、步数、写权限在这里落到 Options。
	UlinCoreKernel::Options options;
	options.systemPrompt = m_systemPrompt.isEmpty()
		? QStringLiteral("You are ULin Core, a concise coding agent. Use the provided tools when they help.")
		: m_systemPrompt;
	options.toolContext.workspaceRoot = m_workspaceRoot.isEmpty()
		? QDir::currentPath()
		: m_workspaceRoot;
	options.toolContext.allowWrites = UlinCoreSettings::instance().allowWrites();
	options.toolThreads = UlinCoreSettings::instance().toolThreads();
	options.maxSteps = UlinCoreSettings::instance().maxSteps();
	m_kernel->setOptions(options);

	connect(m_kernel, &UlinCoreKernel::assistantDelta, this,
		[this](const QString&, const QString& text, const QString& thinking) {
			m_streamedThisTurn = true;
			forwardText(text, thinking);
		});
	connect(m_kernel, &UlinCoreKernel::assistantMessage, this,
		[this](const Message& message) {
			if (!message.text.isEmpty() || !message.thinking.isEmpty()) {
				appendSessionEvent(QStringLiteral("assistant/message"), QJsonObject{
					{ QStringLiteral("message"), QJsonObject{
						{ QStringLiteral("content"), messageContent(message) }
					} }
				});
			}
			if (!m_streamedThisTurn)
				forwardFullMessage(message);
		});
	connect(m_kernel, &UlinCoreKernel::toolStarted, this,
		[this](const QString&, const QString&, const QString& name, const QJsonObject& arguments) {
			appendSessionEvent(QStringLiteral("tool/call"), QJsonObject{
				{ QStringLiteral("name"), name },
				{ QStringLiteral("arguments"), arguments }
			});
			forwardIntentCall(name, arguments);
		});
	connect(m_kernel, &UlinCoreKernel::toolFinished, this,
		[this](const QString&, const QString&, const QString&, const IntentResult& result) {
			m_streamedThisTurn = false;   // 下一段模型输出可能不再流式
			const QString content = result.ok ? result.content : result.error;
			appendSessionEvent(QStringLiteral("tool/result"), QJsonObject{
				{ QStringLiteral("message"), content },
				{ QStringLiteral("ok"), result.ok }
			});
			forwardIntentResult(content, result.ok);
		});
	connect(m_kernel, &UlinCoreKernel::turnFinished, this,
		[this](const TurnResult& result) {
			QJsonObject data;
			if (result.usage.totalTokens() > 0) {
				data.insert(QStringLiteral("usage"), QJsonObject{
					{ QStringLiteral("inputTokens"), result.usage.promptTokens },
					{ QStringLiteral("outputTokens"), result.usage.completionTokens },
					{ QStringLiteral("totalTokens"), result.usage.totalTokens() }
				});
			}
			appendSessionEvent(QStringLiteral("turn/end"), data);
			forwardEvent(QStringLiteral("turn/end"), data);
		});
	connect(m_kernel, &UlinCoreKernel::errorOccurred, this,
		[this](const QString& message) {
			const QString text = QStringLiteral("[ulincore] ") + message;
			const QJsonObject payload{
				{ QStringLiteral("message"), QJsonObject{
					{ QStringLiteral("content"), QJsonArray{
						QJsonObject{
							{ QStringLiteral("type"), QStringLiteral("text") },
							{ QStringLiteral("text"), text }
						}
					} }
				} }
			};
			appendSessionEvent(QStringLiteral("assistant/message"), payload);
			forwardEvent(QStringLiteral("assistant/message"), payload);
		});
}

void UlinCoreMasterAgent::OnOutboundRequest(const char* rpcId, const char* method, const char* argsJson)
{
	const QString name = QString::fromUtf8(method ? method : "");
	const QJsonObject args = QJsonDocument::fromJson(QByteArray(argsJson ? argsJson : "")).object();

	// 接管 DSH 的 outbound RPC：按 method 分发，未实现的显式失败。
	if (name == QStringLiteral("session/prompt")) {
		// 真实 wire 形状：{ request: { sessionId, content: [{type:"text", text}, ...] } }
		const QJsonObject request = args.value(QStringLiteral("request")).toObject();
		m_sessionId = request.value(QStringLiteral("sessionId")).toString(m_sessionId);

		QString text;
		const QJsonArray content = request.value(QStringLiteral("content")).toArray();
		for (const QJsonValue& part : content) {
			const QJsonObject object = part.toObject();
			if (object.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
				text += object.value(QStringLiteral("text")).toString();
		}
		if (text.isEmpty())
			text = request.value(QStringLiteral("text")).toString();

		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		if (text.trimmed().isEmpty())
			return;

		m_streamedThisTurn = false;
		ensureKernel();
		UlinCoreKernel::Options options = m_kernel->options();
		options.toolContext.sessionId = m_sessionId;
		m_kernel->setOptions(options);

		appendSessionEvent(QStringLiteral("turn/start"));
		appendSessionEvent(QStringLiteral("user/message"), QJsonObject{
			{ QStringLiteral("message"), QJsonObject{
				{ QStringLiteral("content"), QJsonArray{
					QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("text") },
						{ QStringLiteral("text"), text }
					}
				} }
			} }
		});
		m_kernel->run(text);
		return;
	}

	if (name == QStringLiteral("session/cancel") || name == QStringLiteral("agent/cancel")) {
		if (m_kernel)
			m_kernel->cancel();
		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		return;
	}

	// 流打开：session/follow 先给一个空快照；job/list 给空名册。
	if (name == QStringLiteral("$takeover/stream-open")) {
		const QString endpoint = args.value(QStringLiteral("endpoint")).toString();
		const QJsonObject streamArgs = args.value(QStringLiteral("args")).toObject();

		if (endpoint == QStringLiteral("session/follow")) {
			// 真实 wire 形状：{ request: { address: { kind, sessionId } } }
			const QJsonObject request = streamArgs.value(QStringLiteral("request")).toObject();
			const QJsonObject address = request.value(QStringLiteral("address")).toObject();
			m_sessionId = address.value(QStringLiteral("sessionId")).toString();

			auto it = m_sessions.find(m_sessionId);
			if (it == m_sessions.end()) {
				SessionRecord record;
				record.id = m_sessionId;
				record.title = QStringLiteral("新会话");
				record.cwd = m_workspaceRoot;
				record.createdAt = record.updatedAt = QDateTime::currentMSecsSinceEpoch();
				m_sessions.insert(record.id, record);
				saveState();
				it = m_sessions.find(m_sessionId);
			}
			if (it != m_sessions.end() && it->events.isEmpty())
				loadSessionLog(*it);

			QJsonArray records;
			if (it != m_sessions.end()) {
				for (const QJsonValue& value : it->events) {
					records.append(QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("event") },
						{ QStringLiteral("event"), value }
					});
				}
				if (m_main)
					m_main->HandleSessionSnapshot(m_sessionId, it->nextSeq - 1, records, false);
			}
			else if (m_main) {
				m_main->HandleSessionSnapshot(m_sessionId, 0, QJsonArray(), false);
			}
			if (m_host)
				m_host->CompleteCall(rpcId, "{}");
			return;
		}
		if (endpoint == QStringLiteral("workspace/follow")) {
			forwardWorkspaceSnapshot();
			if (m_host)
				m_host->CompleteCall(rpcId, "{}");
			return;
		}
		if (endpoint == QStringLiteral("job/list")) {
			if (m_host)
				m_host->CompleteCall(rpcId, "{\"jobs\":[]}");
			return;
		}
		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		return;
	}

	if (name == QStringLiteral("session/create")) {
		const QJsonObject request = args.value(QStringLiteral("request")).toObject();
		const QString workspaceId = request.value(QStringLiteral("workspaceId")).toString();
		const QString cwd = request.value(QStringLiteral("cwd")).toString();
		const QString id = createSessionRecord(workspaceId, cwd);
		const QByteArray result = QJsonDocument(QJsonObject{
			{ QStringLiteral("sessionId"), id }
		}).toJson(QJsonDocument::Compact);
		if (m_host)
			m_host->CompleteCall(rpcId, result.constData());
		return;
	}

	if (name == QStringLiteral("session/list")) {
		const QByteArray result = QJsonDocument(sessionListResult()).toJson(QJsonDocument::Compact);
		if (m_host)
			m_host->CompleteCall(rpcId, result.constData());
		return;
	}

	if (name == QStringLiteral("session/delete")) {
		const QJsonObject request = args.value(QStringLiteral("request")).toObject();
		QString sessionId = request.value(QStringLiteral("sessionId")).toString();
		if (sessionId.isEmpty())
			sessionId = args.value(QStringLiteral("sessionId")).toString();
		m_sessions.remove(sessionId);
		QFile::remove(sessionLogPath(sessionId));
		saveState();
		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		return;
	}

	if (name == QStringLiteral("session/page")) {
		const QByteArray result = QJsonDocument(QJsonObject{
			{ QStringLiteral("records"), QJsonArray() },
			{ QStringLiteral("cursor"), 0 },
			{ QStringLiteral("hasMore"), false }
		}).toJson(QJsonDocument::Compact);
		if (m_host)
			m_host->CompleteCall(rpcId, result.constData());
		return;
	}

	if (name == QStringLiteral("session/projections")) {
		const QByteArray result = QJsonDocument(QJsonObject{
			{ QStringLiteral("values"), QJsonObject() }
		}).toJson(QJsonDocument::Compact);
		if (m_host)
			m_host->CompleteCall(rpcId, result.constData());
		return;
	}

	if (name == QStringLiteral("workspace/create")) {
		const QString path = args.value(QStringLiteral("request")).toObject()
			.value(QStringLiteral("path")).toString();
		WorkspaceRecord workspace;
		workspace.id = QStringLiteral("ulincore-ws-")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
		workspace.title = QDir(path).dirName();
		if (workspace.title.isEmpty())
			workspace.title = QStringLiteral("ULin Core");
		workspace.path = path.isEmpty()
			? (m_workspaceRoot.isEmpty() ? QDir::currentPath() : m_workspaceRoot)
			: path;
		m_workspaces.insert(workspace.id, workspace);
		saveState();

		const QJsonObject body{
			{ QStringLiteral("workspace"), QJsonObject{
				{ QStringLiteral("workspaceId"), workspace.id },
				{ QStringLiteral("title"), workspace.title },
				{ QStringLiteral("path"), workspace.path },
				{ QStringLiteral("sessionIds"), QJsonArray() }
			} },
			{ QStringLiteral("created"), true }
		};
		const QByteArray result = QJsonDocument(body).toJson(QJsonDocument::Compact);
		if (m_host)
			m_host->CompleteCall(rpcId, result.constData());
		return;
	}

	if (name == QStringLiteral("workspace/delete")) {
		const QString workspaceId = args.value(QStringLiteral("request")).toObject()
			.value(QStringLiteral("workspaceId")).toString();
		m_workspaces.remove(workspaceId);
		saveState();
		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		return;
	}

	if (name == QStringLiteral("workspace/archiveSession")
		|| name == QStringLiteral("workspace/unarchiveSession")
		|| name == QStringLiteral("session/archive")) {
		if (m_host)
			m_host->CompleteCall(rpcId, "{}");
		return;
	}

	if (m_host) {
		const QByteArray code = QByteArrayLiteral("ulincore-unsupported");
		const QByteArray message = QStringLiteral("ulincore backend does not implement %1 yet").arg(name).toUtf8();
		qWarning().noquote() << "[UlinCore] unsupported outbound:" << name;
		m_host->FailCall(rpcId, code.constData(), message.constData());
	}
}

void UlinCoreMasterAgent::forwardEvent(const QString& type, const QJsonObject& data)
{
	if (!m_main || m_sessionId.isEmpty())
		return;

	// 所有事件统一包成 session/event 帧，前端按 sessionId 过滤。
	const QJsonObject frame{
		{ QStringLiteral("payload"), QJsonObject{
			{ QStringLiteral("type"), QStringLiteral("session/event") },
			{ QStringLiteral("sessionId"), m_sessionId },
			{ QStringLiteral("event"), QJsonObject{
				{ QStringLiteral("type"), type },
				{ QStringLiteral("data"), data }
			} }
		} }
	};
	m_main->ForwardMuxFrame(frame);
}

void UlinCoreMasterAgent::forwardText(const QString& text, const QString& thinking)
{
	// reasoning 与正文分成不同 chunk 类型下发，前端各自渲染。
	if (!thinking.isEmpty()) {
		forwardEvent(QStringLiteral("assistant/chunk"), QJsonObject{
			{ QStringLiteral("chunk"), QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("reasoning-delta") },
				{ QStringLiteral("text"), thinking }
			} }
		});
	}
	if (!text.isEmpty()) {
		forwardEvent(QStringLiteral("assistant/chunk"), QJsonObject{
			{ QStringLiteral("chunk"), QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("text-delta") },
				{ QStringLiteral("text"), text }
			} }
		});
	}
}

void UlinCoreMasterAgent::forwardIntentCall(const QString& name, const QJsonObject& arguments)
{
	forwardEvent(QStringLiteral("tool/call"), QJsonObject{
		{ QStringLiteral("name"), name },
		{ QStringLiteral("arguments"), arguments }
	});
}

void UlinCoreMasterAgent::forwardIntentResult(const QString& content, bool ok)
{
	forwardEvent(QStringLiteral("tool/result"), QJsonObject{
		{ QStringLiteral("message"), content },
		{ QStringLiteral("ok"), ok }
	});
}

void UlinCoreMasterAgent::forwardFullMessage(const Message& message)
{
	const QJsonArray content = messageContent(message);
	if (content.isEmpty())
		return;

	forwardEvent(QStringLiteral("assistant/message"), QJsonObject{
		{ QStringLiteral("message"), QJsonObject{
			{ QStringLiteral("content"), content }
		} }
	});
}

QJsonArray UlinCoreMasterAgent::messageContent(const Message& message) const
{
	QJsonArray content;
	if (!message.thinking.isEmpty()) {
		content.append(QJsonObject{
			{ QStringLiteral("type"), QStringLiteral("reasoning") },
			{ QStringLiteral("text"), message.thinking }
		});
	}
	if (!message.text.isEmpty()) {
		content.append(QJsonObject{
			{ QStringLiteral("type"), QStringLiteral("text") },
			{ QStringLiteral("text"), message.text }
		});
	}
	return content;
}

QString UlinCoreMasterAgent::sessionLogPath(const QString& sessionId) const
{
	if (m_dataDir.isEmpty() || sessionId.isEmpty())
		return QString();
	return m_dataDir + QStringLiteral("/sessions/") + sessionId + QStringLiteral(".json");
}

void UlinCoreMasterAgent::loadSessionLog(SessionRecord& record) const
{
	const QString path = sessionLogPath(record.id);
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	// 事件日志只存 events 数组，nextSeq 由最大 seq 推回。
	record.events = root.value(QStringLiteral("events")).toArray();
	int maxSeq = 0;
	for (const QJsonValue& value : record.events)
		maxSeq = std::max(maxSeq, value.toObject().value(QStringLiteral("seq")).toInt());
	record.nextSeq = maxSeq + 1;
}

void UlinCoreMasterAgent::saveSessionLog(const SessionRecord& record) const
{
	const QString path = sessionLogPath(record.id);
	if (path.isEmpty())
		return;

	// 事件日志按会话落到 <dataDir>/sessions/<sessionId>.json，索引另存 sessions.json。
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	file.write(QJsonDocument(QJsonObject{
		{ QStringLiteral("id"), record.id },
		{ QStringLiteral("events"), record.events }
	}).toJson(QJsonDocument::Compact));
	file.commit();
}

void UlinCoreMasterAgent::appendSessionEvent(const QString& type, const QJsonObject& data)
{
	if (m_sessionId.isEmpty())
		return;
	auto it = m_sessions.find(m_sessionId);
	if (it == m_sessions.end())
		return;

	// 事件按 seq 单调递增追加，随后立即落盘。
	QJsonObject event = data;
	event.insert(QStringLiteral("type"), type);
	event.insert(QStringLiteral("seq"), it->nextSeq++);
	it->events.append(event);
	it->updatedAt = QDateTime::currentMSecsSinceEpoch();
	saveSessionLog(*it);
}

void UlinCoreMasterAgent::loadState()
{
	m_sessions.clear();
	m_workspaces.clear();
	if (m_dataDir.isEmpty())
		return;

	QFile file(m_dataDir + QStringLiteral("/sessions.json"));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

	for (const QJsonValue& value : root.value(QStringLiteral("workspaces")).toArray()) {
		const QJsonObject object = value.toObject();
		WorkspaceRecord record;
		record.id = object.value(QStringLiteral("id")).toString();
		if (record.id.isEmpty())
			continue;
		record.title = object.value(QStringLiteral("title")).toString();
		record.path = object.value(QStringLiteral("path")).toString();
		m_workspaces.insert(record.id, record);
	}

	for (const QJsonValue& value : root.value(QStringLiteral("sessions")).toArray()) {
		const QJsonObject object = value.toObject();
		SessionRecord record;
		record.id = object.value(QStringLiteral("id")).toString();
		if (record.id.isEmpty())
			continue;
		record.title = object.value(QStringLiteral("title")).toString();
		record.workspaceId = object.value(QStringLiteral("workspaceId")).toString();
		record.cwd = object.value(QStringLiteral("cwd")).toString();
		record.createdAt = static_cast<qint64>(object.value(QStringLiteral("createdAt")).toDouble());
		record.updatedAt = static_cast<qint64>(object.value(QStringLiteral("updatedAt")).toDouble());
		m_sessions.insert(record.id, record);
	}

	qInfo().noquote() << "[UlinCore] loaded" << m_sessions.size()
		<< "sessions from" << m_dataDir;
}

void UlinCoreMasterAgent::saveState() const
{
	if (m_dataDir.isEmpty())
		return;

	// 索引只存工作区与会话元数据，事件正文在各自的日志里。
	QJsonArray workspaces;
	for (const WorkspaceRecord& workspace : m_workspaces) {
		workspaces.append(QJsonObject{
			{ QStringLiteral("id"), workspace.id },
			{ QStringLiteral("title"), workspace.title },
			{ QStringLiteral("path"), workspace.path }
		});
	}

	QJsonArray sessions;
	for (const SessionRecord& session : m_sessions) {
		sessions.append(QJsonObject{
			{ QStringLiteral("id"), session.id },
			{ QStringLiteral("title"), session.title },
			{ QStringLiteral("workspaceId"), session.workspaceId },
			{ QStringLiteral("cwd"), session.cwd },
			{ QStringLiteral("createdAt"), session.createdAt },
			{ QStringLiteral("updatedAt"), session.updatedAt }
		});
	}

	QSaveFile file(m_dataDir + QStringLiteral("/sessions.json"));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return;
	file.write(QJsonDocument(QJsonObject{
		{ QStringLiteral("workspaces"), workspaces },
		{ QStringLiteral("sessions"), sessions }
	}).toJson(QJsonDocument::Compact));
	file.commit();
}

QString UlinCoreMasterAgent::createSessionRecord(const QString& workspaceId, const QString& cwd)
{
	SessionRecord record;
	record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	record.title = QStringLiteral("新会话");
	record.workspaceId = workspaceId;
	record.cwd = cwd;
	if (record.cwd.isEmpty() && m_workspaces.contains(workspaceId))
		record.cwd = m_workspaces.value(workspaceId).path;
	if (record.cwd.isEmpty())
		record.cwd = m_workspaceRoot.isEmpty() ? QDir::currentPath() : m_workspaceRoot;
	record.createdAt = record.updatedAt = QDateTime::currentMSecsSinceEpoch();
	m_sessions.insert(record.id, record);
	// 新会话同时写索引与一条空事件日志。
	saveSessionLog(record);
	saveState();
	qInfo().noquote() << "[UlinCore] session created:" << record.id
		<< "workspace=" << (workspaceId.isEmpty() ? QStringLiteral("(none)") : workspaceId);
	return record.id;
}

QJsonObject UlinCoreMasterAgent::sessionListResult() const
{
	QList<SessionRecord> records = m_sessions.values();
	// 会话列表按 updatedAt 倒序，最近活动的排最前。
	std::sort(records.begin(), records.end(), [](const SessionRecord& a, const SessionRecord& b) {
		return a.updatedAt > b.updatedAt;
	});

	QJsonArray items;
	for (const SessionRecord& record : records) {
		items.append(QJsonObject{
			{ QStringLiteral("sessionId"), record.id },
			{ QStringLiteral("title"), record.title },
			{ QStringLiteral("cwd"), record.cwd },
			{ QStringLiteral("createdAt"), record.createdAt },
			{ QStringLiteral("updatedAt"), record.updatedAt },
			{ QStringLiteral("running"), false }
		});
	}
	return QJsonObject{ { QStringLiteral("items"), items } };
}

void UlinCoreMasterAgent::forwardWorkspaceSnapshot()
{
	if (!m_main)
		return;

	// workspace/follow 打开时先推一份完整工作区快照。
	QJsonArray items;
	for (const WorkspaceRecord& workspace : m_workspaces) {
		items.append(QJsonObject{
			{ QStringLiteral("workspaceId"), workspace.id },
			{ QStringLiteral("title"), workspace.title },
			{ QStringLiteral("path"), workspace.path },
			{ QStringLiteral("sessionIds"), QJsonArray() }
		});
	}
	m_main->HandleWorkspaceSnapshot(items, QJsonArray());
}
} // namespace UlinCore
