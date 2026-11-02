#include "qmlbridge/FileBrowserBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

FileBrowserBridge& FileBrowserBridge::instance()
{
	static FileBrowserBridge bridge;
	return bridge;
}

void FileBrowserBridge::ensureOfficeViewerFiles() const
{
	// 客户端根目录自己的 temp，而不是系统 %TEMP%：可随安装目录一起清理/打包。
	const QString appDir = QCoreApplication::applicationDirPath();
	const QString dirPath = appDir + QStringLiteral("/temp/office-viewer");
	QDir().mkpath(dirPath);
	const QStringList files = { QStringLiteral("viewer.html"), QStringLiteral("viewer.js"),
		QStringLiteral("pdf.min.js"), QStringLiteral("pdf.worker.min.js") };
	for (const QString& name : files) {
		const QString target = dirPath + QLatin1Char('/') + name;
		const QString source = QStringLiteral(":/ULinHub/office-viewer/") + name;
		if (!QFile::exists(target) || QFileInfo(target).size() != QFileInfo(source).size()) {
			QFile::remove(target);
			const bool ok = QFile::copy(source, target);
			if (!ok) {
				// 兼容旧布局：<exe>/resources/office-viewer
				QFile::copy(appDir + QStringLiteral("/resources/office-viewer/") + name, target);
			}
		}
	}
}

QString FileBrowserBridge::officeViewerHtml() const
{
	const QString path = QCoreApplication::applicationDirPath()
		+ QStringLiteral("/temp/office-viewer/viewer.html");
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		qWarning().noquote() << "[OfficeViewer] cannot read" << path;
		return QString();
	}
	return QString::fromUtf8(file.readAll());
}

QString FileBrowserBridge::officeViewerUrl() const
{
	ensureOfficeViewerFiles();
	const QString path = QCoreApplication::applicationDirPath()
		+ QStringLiteral("/temp/office-viewer/");
	qInfo().noquote() << "[OfficeViewer] base=" << path
		<< "exists=" << QFile::exists(path + QStringLiteral("viewer.html"));
	return QUrl::fromLocalFile(path).toString();
}

FileBrowserBridge::FileBrowserBridge(QObject* parent)
	: QObject(parent)
{
	ensureOfficeViewerFiles();
	// 接线：领域服务的通知原样转成 QML 属性通知。
	connect(&m_service, &FileBrowserService::rootPathChanged,
		this, &FileBrowserBridge::rootPathChanged);
	connect(&m_service, &FileBrowserService::currentPathChanged,
		this, &FileBrowserBridge::currentPathChanged);
	connect(&m_service, &FileBrowserService::entriesChanged,
		this, &FileBrowserBridge::entriesChanged);
	connect(&m_service, &FileBrowserService::fileChanged,
		this, &FileBrowserBridge::fileChanged);
	connect(&m_service, &FileBrowserService::errorChanged,
		this, &FileBrowserBridge::errorChanged);
}
