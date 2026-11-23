#include <QUrlQuery>
#include "common/agent/dsh/pluginmarket/PluginMarketClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace
{
	const char* const kRegistryPath = "/dsh-market/registry";
	const char* const kInstalledPath = "/dsh-market/installed";
	const char* const kStatusPath = "/dsh-market/status";
	const char* const kInstallPath = "/dsh-market/install";
	const char* const kUninstallPath = "/dsh-market/uninstall";
	const char* const kUpdatePath = "/dsh-market/update";
	const char* const kRestartPath = "/dsh-market/restart";
	const char* const kApproveBuildsPath = "/dsh-market/approve-builds";
	const char* const kLogsPath = "/dsh-market/logs";

	QString bodyPreview(const QByteArray& raw)   // 失败的响应体可能不是 JSON（例如网关 502），截断后再展示
	{
		return QString::fromUtf8(raw).trimmed().left(300);
	}

	QUrl originOf(const QUrl& base)   // 纯 origin（scheme://host:port），给 POST 的 Origin 头用
	{
		QUrl url = base;
		url.setPath(QString());
		url.setQuery(QString());
		url.setFragment(QString());
		return url;
	}
}

void PluginMarketClient::setAuthToken(const QString& token)
{
	m_authToken = token;
}

void PluginMarketClient::setNetworkAccessManager(QNetworkAccessManager* nam)
{
	if (!nam || nam == m_nam)
		return;
	QNetworkAccessManager* old = m_nam;
	m_nam = nam;
	if (old)
		old->deleteLater();
}

PluginMarketClient::PluginMarketClient(QObject* parent)
	: QObject(parent)
	, m_nam(new QNetworkAccessManager(this))
{
}

void PluginMarketClient::setBaseUrl(const QUrl& url)
{
	m_baseUrl = url;
}

// 由 baseUrl 拼出接口地址：只借它的 scheme/host/port，path 重设、query 与 fragment 清掉。
// 不能用 `QUrl(base.toString(QUrl::RemovePath) + path)`：带令牌的 baseUrl 会把接口地址当成 token 的值、
// 请求落到 `GET /`（无 cookie → 401；有 cookie → 303 到 index.html，HTML 被当 JSON，市场显示"0 个插件"）。
QUrl PluginMarketClient::endpointUrl(const QString& path) const
{
	QUrl url = m_baseUrl;
	url.setPath(path);
	url.setQuery(QString());
	url.setFragment(QString());
	if (!m_authToken.isEmpty()) {
		QUrlQuery query;   // 新版 dshmarket(1.66.x) 要求宿主身份：沿用引擎握手用的 ?token=<launch token>，否则 401 host requires authentication
		query.addQueryItem(QStringLiteral("token"), m_authToken);
		url.setQuery(query);
	}
	return url;
}

QString PluginMarketClient::registryPath()
{
	return QLatin1String(kRegistryPath);
}

QString PluginMarketClient::installedPath()
{
	return QLatin1String(kInstalledPath);
}

QString PluginMarketClient::statusPath()
{
	return QLatin1String(kStatusPath);
}

QString PluginMarketClient::installPath()
{
	return QLatin1String(kInstallPath);
}

QString PluginMarketClient::uninstallPath()
{
	return QLatin1String(kUninstallPath);
}

QString PluginMarketClient::updatePath()
{
	return QLatin1String(kUpdatePath);
}

QString PluginMarketClient::restartPath()
{
	return QLatin1String(kRestartPath);
}

QString PluginMarketClient::approveBuildsPath()
{
	return QLatin1String(kApproveBuildsPath);
}

void PluginMarketClient::fetchRegistry()
{
	const QString path = registryPath();
	QNetworkRequest request(endpointUrl(path));
	qInfo().noquote() << "[Market] GET" << path;

	// 必须设超时：引擎接了连接却不回（插件未加载、路由挂住）时会一直悬着，界面永远停在"正在等待最新数据"；
	// 超时即失败并退回快照。
	request.setTransferTimeout(180000);
	QNetworkReply* reply = m_nam->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		const QString transportError = reply->errorString();
		const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
		reply->deleteLater();

		if (failed) {
			qWarning().noquote() << "[PluginMarketClient] registry request failed, status=" << status
				<< "error=" << transportError
				<< "body=" << bodyPreview(raw);
			emit registryFailed(transportError, status);
			if (status >= 500)
				fetchDiagnosticLogs(path);
			return;
		}

		const QJsonObject root = QJsonDocument::fromJson(raw).object();
		const QString source = root.value(QStringLiteral("source")).toString();
		const QJsonArray plugins = root.value(QStringLiteral("registry")).toObject()
			.value(QStringLiteral("plugins")).toArray();

		if (plugins.isEmpty())   // 契约没满足就喊出来：这类"静默 0 个插件"过去只能靠猜，把状态码/字节数/响应开头都打出来
			qWarning().noquote() << "[PluginMarketClient] registry payload unexpected"
			<< "status=" << status << "bytes=" << raw.size() << "body=" << bodyPreview(raw);

		qInfo().noquote() << "[PluginMarketClient] registry loaded"
			<< "source=" << source
			<< "plugins=" << plugins.size();

		emit registryLoaded(plugins, source);
		});
}

void PluginMarketClient::fetchStatus()
{
	const QString path = statusPath();
	QNetworkRequest request(endpointUrl(path));
	// 状态是几百字节的本地 JSON：短超时即可；轮询期间偶发失败不该刷警告（市场包没装时必然 404）。
	request.setTransferTimeout(1800);
	QNetworkReply* reply = m_nam->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
		reply->deleteLater();

		if (failed) {
			emit statusLoaded(QJsonObject());
			return;
		}

		QJsonParseError parseError{};
		const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
			emit statusLoaded(QJsonObject());
			return;
		}
		emit statusLoaded(doc.object());
	});
}

void PluginMarketClient::fetchInstalled()
{
	const QString path = installedPath();
	QNetworkRequest request(endpointUrl(path));
	qInfo().noquote() << "[Market] GET" << path;

	// 必须设超时：引擎接了连接却不回（插件未加载、路由挂住）时会一直悬着，界面永远停在"正在等待最新数据"；
	// 超时即失败并退回快照。
	request.setTransferTimeout(180000);
	QNetworkReply* reply = m_nam->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		const QString transportError = reply->errorString();
		const bool failed = reply->error() != QNetworkReply::NoError || status >= 400;
		reply->deleteLater();

		if (failed) {
			qWarning().noquote() << "[PluginMarketClient] installed request failed, status=" << status
				<< "error=" << transportError
				<< "body=" << bodyPreview(raw);
			return;
		}

		const QJsonObject root = QJsonDocument::fromJson(raw).object();
		emit installedLoaded(root.value(QStringLiteral("installed")).toObject());
		});
}

void PluginMarketClient::installPlugin(const QString& url)
{
	if (url.isEmpty())
		return;

	QJsonObject body;
	body.insert(QStringLiteral("url"), url);
	post(installPath(), body);
}

void PluginMarketClient::uninstallPlugin(const QString& name)
{
	QJsonObject body;
	body.insert(QStringLiteral("name"), name);
	post(uninstallPath(), body);
}

void PluginMarketClient::updatePlugin(const QString& name)
{
	QJsonObject body;
	body.insert(QStringLiteral("name"), name);
	post(updatePath(), body);
}

void PluginMarketClient::restartServer()
{
	post(restartPath(), QJsonObject());
}

void PluginMarketClient::approveBuilds(const QStringList& packages)
{
	if (packages.isEmpty())
		return;

	QJsonArray list;
	for (const QString& package : packages)
		list.append(package);
	QJsonObject body;
	body.insert(QStringLiteral("packages"), list);
	post(approveBuildsPath(), body);
}

void PluginMarketClient::post(const QString& path, const QJsonObject& body)
{
	QNetworkRequest request(endpointUrl(path));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader("Origin", originOf(m_baseUrl).toString().toUtf8());   // 市场服务端要求同源（Origin 的 host 必须等于 Host）；用纯 origin，别把令牌带进去

	qInfo().noquote() << QStringLiteral("[Market] POST %1 body=%2").arg(path, QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));

	QNetworkReply* reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		reply->deleteLater();

		const bool transportError = reply->error() != QNetworkReply::NoError;
		const QJsonObject root = QJsonDocument::fromJson(raw).object();
		QString serverError = root.value(QStringLiteral("error")).toString();

		// dshmarket 的 install/update 失败回包会带 ignoredBuilds；成功但跳过构建时也可能带。
		// 先把它抛给界面，后面的通用错误文案才不会把它盖掉。
		QStringList blocked;
		for (const QJsonValue& value : root.value(QStringLiteral("ignoredBuilds")).toArray()) {
			if (value.isString())
				blocked.append(value.toString());
		}
		if (!blocked.isEmpty()) {
			qInfo().noquote() << "[Market] 构建脚本被拦截:" << blocked.join(QStringLiteral(", "));
			emit buildsBlocked(blocked);
		}

		if (transportError || status >= 400) {
			if (serverError.isEmpty())   // 服务端可能返回非 JSON（如网关 502），此时展示原始状态码与正文
				serverError = QStringLiteral("HTTP %1 %2").arg(status).arg(bodyPreview(raw));
			qWarning().noquote() << "[Market] POST" << path
				<< "failed, status=" << status
				<< "error=" << serverError;
			emit operationFailed(path, serverError, status);

			if (status >= 500 || transportError)   // 5xx / 传输错误多半只在服务端可见（pnpm、网络、路由抛异常），顺手拉 dshmarket 日志定位 502
				fetchDiagnosticLogs(path);
			return;
		}

		qInfo().noquote() << "[Market] POST" << path << "ok, status=" << status;
		if (path == approveBuildsPath()) {
			QStringList approved;
			for (const QJsonValue& value : root.value(QStringLiteral("approved")).toArray()) {
				if (value.isString())
					approved.append(value.toString());
			}
			emit buildsApproved(approved);
		}
		emit operationCompleted(path);
		});
}

void PluginMarketClient::fetchDiagnosticLogs(const QString& reason)
{
	QNetworkRequest request(endpointUrl(QLatin1String(kLogsPath)));

	// 引擎未响应时避免无限悬着；超时按失败处理，只记日志。
	request.setTransferTimeout(180000);
	QNetworkReply* reply = m_nam->get(request);
	connect(reply, &QNetworkReply::finished, this, [reason, reply]() {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray raw = reply->readAll();
		reply->deleteLater();

		qInfo().noquote() << "[Market] fetchDiagnosticLogs reason=" << reason << "status=" << status;
		const QString text = QString::fromUtf8(raw).trimmed();
		if (text.isEmpty()) {
			qInfo().noquote() << "[dsh-market-log] (empty)";
			return;
		}
		for (const QString& line : text.split(QLatin1Char('\n')))
			qInfo().noquote() << "[dsh-market-log]" << line;
		});
}
