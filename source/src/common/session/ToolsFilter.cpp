// 见 ToolsFilter.h：数据层（HTTP/JSON）+ 视图模型（摊平的行）。
// 形状计算从旧壳 common/appearance/TopBarTools.cpp 原样搬入（2026-10-10），语义一字不改。

#include "common/session/ToolsFilter.h"

#include <QDebug>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <algorithm>

namespace
{
	const char kEndpointPath[] = "/api/tools-filter";  // 插件端点路径（ToolsFilterPlugin 注册的）
	const char kDefaultDirectoryName[] = "Default";  // 程序自动建立的目录名（插件侧同名）

	QString normalizedDirectoryName(const QString& raw)
	{
		return raw.trimmed();
	}

	QString directoryKey(const QString& name)
	{
		return normalizedDirectoryName(name).toLower();
	}

	// 配置里 IsExpanded 是字符串 "True"/"False"（也可能是 bool）——"False" 表示整组隐藏
	bool flagIsFalse(const QJsonValue& value)
	{
		if (value.isBool())
			return !value.toBool();
		return value.toString().compare(QLatin1String("false"), Qt::CaseInsensitive) == 0;
	}

	// ToolsList 里的一项：可见时就是工具名字符串，隐藏时是 { ToolName, IsVisible:"False" }
	QString rowToolName(const QJsonValue& row)
	{
		if (row.isString())
			return row.toString();
		return row.toObject().value(QStringLiteral("ToolName")).toString();
	}

	bool rowVisible(const QJsonValue& row)
	{
		if (row.isString())
			return true;
		return !flagIsFalse(row.toObject().value(QStringLiteral("IsVisible")));
	}

	// 插件没给 directory 时的兜底分组（与原实现同一张表）
	QString builtinFunctionDirectory(const QString& toolName)
	{
		static const QHash<QString, QString> directories{
			{ QStringLiteral("shell"), QStringLiteral("Host") },
			{ QStringLiteral("apply_patch"), QStringLiteral("Files") },
			{ QStringLiteral("read_file"), QStringLiteral("Files") },
			{ QStringLiteral("write_file"), QStringLiteral("Files") },
			{ QStringLiteral("list_dir"), QStringLiteral("Files") },
			{ QStringLiteral("web_search"), QStringLiteral("Web") },
			{ QStringLiteral("web_fetch"), QStringLiteral("Web") },
			{ QStringLiteral("todo_write"), QStringLiteral("Plan") },
			{ QStringLiteral("workflow"), QStringLiteral("Workflow") },
			{ QStringLiteral("ralph"), QStringLiteral("Workflow") },
			{ QStringLiteral("skill"), QStringLiteral("Skill") },
			{ QStringLiteral("ask_user_question"), QStringLiteral("Interaction") },
			{ QStringLiteral("present"), QStringLiteral("Output") },
			{ QStringLiteral("run_code"), QStringLiteral("Code") },
			{ QStringLiteral("cordis_inspect_list"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_inspect_query"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_inspect_self"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_define"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_run"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_stop"), QStringLiteral("Cordis") },
			{ QStringLiteral("cordis_undefine"), QStringLiteral("Cordis") },
			{ QStringLiteral("gettools"), QStringLiteral("ToolsFilterPlugin") },
		};

		const auto exact = directories.constFind(toolName);
		if (exact != directories.constEnd())
			return exact.value();
		if (toolName.startsWith(QStringLiteral("mcp_")))
			return QStringLiteral("MCP");
		return QString();
	}
}

ToolsFilter::ToolsFilter(QObject* parent)
	: QAbstractListModel(parent)
	, m_nam(new QNetworkAccessManager(this))
{
}

void ToolsFilter::setBaseUrl(const QUrl& url)
{
	m_baseUrl = url;
}

QString ToolsFilter::defaultDirectoryName()
{
	return QString::fromLatin1(kDefaultDirectoryName);
}

// ------------------------------------------------------------------
// 形状计算：插件目录 + 已存配置 → 目录树（原 TopBarTools::buildDirectories）
// ------------------------------------------------------------------

QVector<ToolsFilter::Directory> ToolsFilter::buildDirectories(const QJsonArray& tools, const QJsonObject& storedConfig)
{
	QVector<Directory> directories;
	QHash<QString, int> indexByKey;
	QHash<QString, QString> directoryOfTool;
	QHash<QString, bool> storedVisible;

	auto ensureDirectory = [&](const QString& rawName) -> int {
		const QString name = normalizedDirectoryName(rawName);
		const QString key = name.toLower();
		const auto existing = indexByKey.constFind(key);
		if (existing != indexByKey.constEnd())
			return existing.value();

		Directory entry;
		entry.name = name;
		indexByKey.insert(key, directories.size());
		directories.append(entry);
		return directories.size() - 1;
		};

	// 空目录也留着 —— 配置里声明过的目录都要在界面上露面
	const QJsonArray filterList = storedConfig.value(QStringLiteral("FilterList")).toArray();
	const bool autoGroup = filterList.isEmpty();
	for (const QJsonValue& directoryValue : filterList) {
		const QJsonObject directory = directoryValue.toObject()
			.value(QStringLiteral("Directory")).toObject();
		if (directory.isEmpty())
			continue;

		const QString name = normalizedDirectoryName(
			directory.value(QStringLiteral("DirectoryName")).toString());
		const QString key = name.toLower();
		const bool isNewDirectory = !indexByKey.contains(key);
		const int index = ensureDirectory(name);
		if (isNewDirectory) {
			directories[index].description = directory.value(QStringLiteral("Description")).toString();
			directories[index].groupHidden = flagIsFalse(directory.value(QStringLiteral("IsExpanded")));
		}

		const QJsonArray toolsList = directory.value(QStringLiteral("ToolsList")).toArray();
		for (const QJsonValue& row : toolsList) {
			const QString toolName = rowToolName(row);
			if (toolName.isEmpty())
				continue;
			// 同一工具出现在多个目录时第一个认领的说话；可见性相反，后写覆盖先写
			if (!directoryOfTool.contains(toolName))
				directoryOfTool.insert(toolName, key);
			storedVisible.insert(toolName, rowVisible(row));
		}
	}

	int defaultIndex = -1;  // 无存储配置时，插件给的 directory 就是初始目录，缺字段才回退 Default
	if (!autoGroup)
		defaultIndex = ensureDirectory(defaultDirectoryName());

	// 工具顺序跟着插件返回的顺序
	for (const QJsonValue& value : tools) {
		const QJsonObject tool = value.toObject();
		Tool entry;
		entry.name = tool.value(QStringLiteral("name")).toString();
		if (entry.name.isEmpty())
			continue;
		entry.description = tool.value(QStringLiteral("description")).toString();
		const QJsonValue parameters = tool.value(QStringLiteral("parameters"));
		if (parameters.isObject())
			entry.parameters = QString::fromUtf8(QJsonDocument(parameters.toObject()).toJson(QJsonDocument::Compact));

		const auto stored = storedVisible.constFind(entry.name);
		entry.visible = stored == storedVisible.constEnd() ? true : stored.value();

		// 工具归到哪个目录：**找，找不到再新建**。三级回退，顺序不能反：
		//   ① 已存配置的归属（配置是权威 —— 用户在界面上改过的分组不能因为插件换了分组就复位）
		//   ② 插件随 tools[] 给的 directory（新装的工具、配置里还没有它）
		//   ③ 内置功能表 / Default（兜底）
		// ⚠️ 这里**不能**把 index 预置成 defaultIndex：那样 ①④ 都会"命中"Default，
		//    表现为所有工具都堆在 Default、其它目录恒 0（2026-10-10 实测就是这个症状）。
		int index = -1;
		if (autoGroup) {
			QString directory = tool.value(QStringLiteral("directory")).toString().trimmed();
			if (directory.isEmpty())
				directory = builtinFunctionDirectory(entry.name);
			if (directory.isEmpty())
				directory = defaultDirectoryName();
			index = ensureDirectory(directory);
		}
		else {
			const QString storedDirectory = directoryOfTool.value(entry.name);
			if (!storedDirectory.isEmpty())
				index = indexByKey.value(directoryKey(storedDirectory), -1);
			if (index < 0) {
				QString directory = tool.value(QStringLiteral("directory")).toString().trimmed();
				if (directory.isEmpty())
					directory = builtinFunctionDirectory(entry.name);
				index = directory.isEmpty() ? -1 : ensureDirectory(directory);
			}
			if (index < 0) {
				if (defaultIndex < 0)
					defaultIndex = ensureDirectory(defaultDirectoryName());
				index = defaultIndex;
			}
		}

		if (index >= 0 && index < directories.size())
			directories[index].tools.append(entry);
	}

	if (autoGroup && directories.isEmpty())
		ensureDirectory(defaultDirectoryName());

	return directories;
}

QJsonArray ToolsFilter::buildToolsList(const QVector<Tool>& tools)
{
	QJsonArray list;
	for (const Tool& tool : tools) {
		if (tool.visible) {
			list.append(tool.name);   // 可见：只写名字
			continue;
		}
		QJsonObject row;
		row.insert(QStringLiteral("ToolName"), tool.name);
		row.insert(QStringLiteral("IsVisible"), QStringLiteral("False"));
		list.append(row);
	}
	return list;
}

QJsonObject ToolsFilter::buildDocument(const QVector<Directory>& directories, bool dropGuidance,
	const QStringList& hideContexts)
{
	QJsonArray filterList;
	for (const Directory& directory : directories) {
		QJsonObject header;
		header.insert(QStringLiteral("DirectoryName"), directory.name);
		if (!directory.description.isEmpty())
			header.insert(QStringLiteral("Description"), directory.description);
		// IsExpanded 是**筛选语义**：整组隐藏时写字符串 "False"（插件按它编译过滤表）
		if (directory.groupHidden)
			header.insert(QStringLiteral("IsExpanded"), QStringLiteral("False"));

		QJsonObject node;
		node.insert(QStringLiteral("Directory"), header);
		node.insert(QStringLiteral("ToolsList"), buildToolsList(directory.tools));
		filterList.append(node);
	}

	QJsonObject document;
	document.insert(QStringLiteral("FilterList"), filterList);
	document.insert(QStringLiteral("DropGuidance"), dropGuidance);
	if (!hideContexts.isEmpty()) {
		QJsonArray contexts;
		for (const QString& name : hideContexts)
			contexts.append(name);
		document.insert(QStringLiteral("HideContexts"), contexts);
	}
	return document;
}

// ------------------------------------------------------------------
// 模型：目录树摊平成行
// ------------------------------------------------------------------

void ToolsFilter::rebuildRows()
{
	beginResetModel();
	m_rows.clear();
	for (int d = 0; d < m_directories.size(); ++d) {
		const Directory& directory = m_directories.at(d);

		Row header;
		header.kind = QStringLiteral("directory");
		header.directoryIndex = d;
		m_rows.append(header);

		if (!directory.expanded)
			continue;
		for (int t = 0; t < directory.tools.size(); ++t) {
			Row row;
			row.kind = QStringLiteral("tool");
			row.directoryIndex = d;
			row.toolIndex = t;
			m_rows.append(row);
		}
	}
	endResetModel();
	emit summaryChanged();
}

int ToolsFilter::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_rows.size();
}

QVariant ToolsFilter::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
		return QVariant();

	const Row& row = m_rows.at(index.row());
	if (row.directoryIndex < 0 || row.directoryIndex >= m_directories.size())
		return QVariant();
	const Directory& directory = m_directories.at(row.directoryIndex);

	switch (role) {
	case KindRole: return row.kind;
	case DirectoryRole: return directory.name;
	case DescriptionRole:
		return row.kind == QLatin1String("tool") && row.toolIndex >= 0 && row.toolIndex < directory.tools.size()
			? directory.tools.at(row.toolIndex).description
			: directory.description;
	case CountRole: return directory.tools.size();
	case HiddenCountRole: {
		int hidden = 0;
		for (const Tool& tool : directory.tools) {
			if (!tool.visible)
				++hidden;
		}
		return hidden;
	}
	case VisibleRole:
		return row.toolIndex >= 0 && row.toolIndex < directory.tools.size()
			? directory.tools.at(row.toolIndex).visible
			: true;
	case HiddenRole: return directory.groupHidden;
	case ExpandedRole: return directory.expanded;
	case ToolNameRole:
		return row.toolIndex >= 0 && row.toolIndex < directory.tools.size()
			? directory.tools.at(row.toolIndex).name
			: QString();
	case ParametersRole:
		return row.toolIndex >= 0 && row.toolIndex < directory.tools.size()
			? directory.tools.at(row.toolIndex).parameters
			: QString();
	default:
		return QVariant();
	}
}

QHash<int, QByteArray> ToolsFilter::roleNames() const
{
	return {
		{ KindRole, "kind" },
		{ DirectoryRole, "directory" },
		{ DescriptionRole, "description" },
		{ CountRole, "count" },
		{ HiddenCountRole, "hiddenCount" },
		{ VisibleRole, "toolVisible" },
		{ HiddenRole, "hidden" },
		{ ExpandedRole, "expandedState" },
		{ ToolNameRole, "toolName" },
		{ ParametersRole, "parameters" },
	};
}

int ToolsFilter::visibleCount() const
{
	int n = 0;
	for (const Directory& directory : m_directories)
		for (const Tool& tool : directory.tools)
			if (tool.visible)
				++n;
	return n;
}

int ToolsFilter::totalCount() const
{
	int n = 0;
	for (const Directory& directory : m_directories)
		n += directory.tools.size();
	return n;
}

void ToolsFilter::syncSummary()
{
	emit summaryChanged();
}

ToolsFilter::Directory* ToolsFilter::findDirectory(const QString& name)
{
	const QString key = directoryKey(name);
	for (Directory& directory : m_directories) {
		if (directoryKey(directory.name) == key)
			return &directory;
	}
	return nullptr;
}

ToolsFilter::Tool* ToolsFilter::findTool(const QString& name)
{
	for (Directory& directory : m_directories) {
		for (Tool& tool : directory.tools) {
			if (tool.name == name)
				return &tool;
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------
// 界面动作
// ------------------------------------------------------------------

void ToolsFilter::toggleDirectory(const QString& directoryName)
{
	Directory* directory = findDirectory(directoryName);
	if (!directory)
		return;
	directory->expanded = !directory->expanded;
	rebuildRows();
}

void ToolsFilter::setToolVisible(const QString& toolName, bool visible)
{
	Tool* tool = findTool(toolName);
	if (!tool || tool->visible == visible)
		return;
	tool->visible = visible;

	// 单行更新（整表重建会让 ListView 丢滚动位置/勾选焦点）
	for (int i = 0; i < m_rows.size(); ++i) {
		const Row& row = m_rows.at(i);
		if (row.kind != QLatin1String("tool") || row.directoryIndex < 0)
			continue;
		const Directory& directory = m_directories.at(row.directoryIndex);
		if (row.toolIndex < 0 || row.toolIndex >= directory.tools.size())
			continue;
		if (directory.tools.at(row.toolIndex).name != toolName)
			continue;
		const QModelIndex idx = index(i, 0);
		emit dataChanged(idx, idx, { VisibleRole });
		break;
	}
	syncSummary();
}

void ToolsFilter::setDirectoryHidden(const QString& directoryName, bool hidden)
{
	Directory* directory = findDirectory(directoryName);
	if (!directory || directory->groupHidden == hidden)
		return;
	directory->groupHidden = hidden;

	for (int i = 0; i < m_rows.size(); ++i) {
		const Row& row = m_rows.at(i);
		if (row.kind != QLatin1String("directory") || row.directoryIndex < 0)
			continue;
		if (directoryKey(m_directories.at(row.directoryIndex).name) != directoryKey(directoryName))
			continue;
		const QModelIndex idx = index(i, 0);
		emit dataChanged(idx, idx, { HiddenRole });
		break;
	}
}

void ToolsFilter::setAllVisible(bool visible)
{
	for (Directory& directory : m_directories) {
		for (Tool& tool : directory.tools)
			tool.visible = visible;
	}
	rebuildRows();
}

// ------------------------------------------------------------------
// 网络
// ------------------------------------------------------------------

void ToolsFilter::setBusy(bool value)
{
	if (m_busy == value)
		return;
	m_busy = value;
	emit busyChanged();
}

void ToolsFilter::setStatus(const QString& text)
{
	if (m_status == text)
		return;
	m_status = text;
	emit statusChanged();
}

void ToolsFilter::refresh(const QString& sessionId)
{
	if (sessionId.isEmpty()) {
		setStatus(qtTrId("toolfilter_no_session"));
		return;
	}
	if (m_baseUrl.isEmpty()) {
		setStatus(qtTrId("toolfilter_server_not_ready"));
		return;
	}

	m_sessionId = sessionId;
	setBusy(true);
	setStatus(qtTrId("toolfilter_loading"));

	QUrl url = m_baseUrl;  // 借 baseUrl 的 scheme/host/port：带启动令牌，直拼会落到 GET / 拿回 HTML
	url.setPath(QString::fromLatin1(kEndpointPath));
	url.setQuery(QString());
	url.setFragment(QString());
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("session"), sessionId);
	url.setQuery(query);

	QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		const QString transportError = reply->errorString();
		const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
		reply->deleteLater();

		setBusy(false);

		if (failed) {
			// 插件缺席时会落到 /api 前缀路由，拿回的不是我们的 JSON
			setStatus(qtTrId("toolfilter_plugin_no_response"));
			qWarning().noquote() << "[ToolsFilter] GET failed status=" << status << "error=" << transportError;
			return;
		}

		const QJsonObject root = QJsonDocument::fromJson(raw).object();
		if (!root.value(QStringLiteral("tools")).isArray()) {
			setStatus(qtTrId("toolfilter_plugin_unavailable"));
			qWarning().noquote() << "[ToolsFilter] unexpected payload";
			return;
		}

		m_degraded = root.value(QStringLiteral("degraded")).toBool();
		m_configFound = root.value(QStringLiteral("configFound")).toBool();
		m_dropGuidance = root.value(QStringLiteral("dropGuidance")).toBool(true);
		m_hideContexts.clear();
		for (const QJsonValue& value : root.value(QStringLiteral("hideContexts")).toArray()) {
			const QString name = value.toString();
			if (!name.isEmpty())
				m_hideContexts.append(name);
		}

		// 已存的 FilterList 在 GET 应答的 `storedConfig` 里（不是 config —— 原实现就是这个字段名）
		const QJsonObject stored = root.value(QStringLiteral("storedConfig")).toObject();
		m_directories = buildDirectories(root.value(QStringLiteral("tools")).toArray(), stored);
		rebuildRows();

		m_loaded = true;
		emit loadedChanged();

		// 该会话还没有配置文件：建一份"全部可见"的初始版写回（原 ShellController 的 ensureSession 语义，
		// 少了这一步，插件下次编译过滤表时会认为"这个会话没配过"）
		if (!m_configFound) {
			qInfo().noquote() << "[ToolsFilter] 该会话没有配置文件，写入初始版 session=" << m_sessionId;
			save();
			return;
		}

		setStatus(qtTrId("toolfilter_summary_full_fmt")
			.arg(totalCount()).arg(m_directories.size()).arg(totalCount() - visibleCount()));
		statusSummaryOrEmpty();
	});
}

// 目录为空 / 服务器只答出全局层时的状态行（与原实现同一套 id）
void ToolsFilter::statusSummaryOrEmpty()
{
	if (m_directories.isEmpty() || totalCount() == 0) {
		setStatus(qtTrId("toolfilter_no_filterable_tools"));
		return;
	}

	QString text = qtTrId("toolfilter_summary_full_fmt")
		.arg(totalCount()).arg(m_directories.size()).arg(totalCount() - visibleCount());
	if (m_degraded)
		text += qtTrId("toolfilter_global_layer_only_suffix");
	setStatus(text);
}

void ToolsFilter::save()
{
	if (m_sessionId.isEmpty()) {
		setStatus(qtTrId("toolfilter_no_session"));
		return;
	}
	if (m_baseUrl.isEmpty()) {
		setStatus(qtTrId("toolfilter_server_not_ready"));
		return;
	}

	setBusy(true);
	setStatus(qtTrId("toolfilter_saving"));

	QJsonObject payload;
	payload.insert(QStringLiteral("sessionId"), m_sessionId);
	payload.insert(QStringLiteral("config"), buildDocument(m_directories, m_dropGuidance, m_hideContexts));

	QUrl url = m_baseUrl;
	url.setPath(QString::fromLatin1(kEndpointPath));
	url.setQuery(QString());
	url.setFragment(QString());

	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply* reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QString transportError = reply->errorString();
		const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
		reply->deleteLater();

		setBusy(false);

		if (failed) {
			const QString message = status > 0
				? qtTrId("toolfilter_save_failed_http_fmt").arg(status)
				: qtTrId("toolfilter_save_failed_reason_fmt").arg(transportError);
			setStatus(message);
			qWarning().noquote() << "[ToolsFilter] POST failed status=" << status << "error=" << transportError;
			return;
		}

		setStatus(qtTrId("toolfilter_summary_fmt").arg(totalCount()).arg(totalCount() - visibleCount()));
	});
}
