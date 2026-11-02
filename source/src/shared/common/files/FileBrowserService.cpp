#include "common/files/FileBrowserService.h"
#include "common/files/OfficeTextExtractor.h"
#include "common/files/OfficeConverter.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace
{
QString humanSize(qint64 bytes)
{
	if (bytes < 1024)
		return QStringLiteral("%1 B").arg(bytes);
	if (bytes < 1024 * 1024)
		return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
	return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}
} // namespace

FileBrowserService::FileBrowserService(QObject* parent)
	: QObject(parent)
{
}

void FileBrowserService::setOfficeBackend(QObject* backend)
{
	if (m_officeBackend == backend)
		return;
	if (m_officeBackend)
		disconnect(m_officeBackend, nullptr, this, nullptr);
	m_officeBackend = backend;
	if (!backend)
		return;
	connect(backend, SIGNAL(officeConversionFinished(QString,QString,QString)), this,
		SLOT(onOfficeBackendFinished(QString,QString,QString)));
}

void FileBrowserService::onOfficeBackendFinished(const QString& source, const QString& pdfPath, const QString& error)
{
	if (QDir::cleanPath(source) != m_filePath)
		return;
	m_officeConverting = false;
	if (!error.isEmpty() || !QFileInfo::exists(pdfPath) || QFileInfo(pdfPath).size() <= 0) {
		m_officeError = error.isEmpty() ? QStringLiteral("LibreOffice 转换失败") : error;
		emit fileChanged();
		return;
	}
	QFile pdf(pdfPath);
	if (!pdf.open(QIODevice::ReadOnly)) {
		m_officeError = QStringLiteral("无法读取转换后的 PDF");
		emit fileChanged();
		return;
	}
	m_officePdfBase64 = QString::fromLatin1(pdf.readAll().toBase64());
	m_officeError.clear();
	emit fileChanged();
}


bool FileBrowserService::isInsideRoot(const QString& path) const
{
	if (m_rootPath.isEmpty())
		return false;
	const QString root = QDir::cleanPath(QFileInfo(m_rootPath).canonicalFilePath());
	const QString target = QDir::cleanPath(QFileInfo(path).canonicalFilePath());
	if (root.isEmpty() || target.isEmpty())
		return false;
	const QString rootPrefix = root + QLatin1Char('/');
	return target == root || target.startsWith(rootPrefix, Qt::CaseInsensitive);
}

void FileBrowserService::setError(const QString& text)
{
	if (m_error == text)
		return;
	m_error = text;
	emit errorChanged();
}

void FileBrowserService::clearFile()
{
	m_text.clear();
	m_fileName.clear();
	m_textMode = false;
	m_filePath.clear();
	m_officeKind.clear();
	m_officeText.clear();
	m_officePdfBase64.clear();
	m_officeError.clear();
	if (m_convertProc) {
		m_convertProc->kill();
		m_convertProc->deleteLater();
		m_convertProc = nullptr;
	}
	m_officeConverting = false;
	if (!m_tempPdfPath.isEmpty()) {
		QFile::remove(m_tempPdfPath);
		m_tempPdfPath.clear();
	}
	m_buffer.clear();
	m_fromBuffer = false;
	m_lineOffsets.clear();
	m_pageIndex = 0;
	m_pageCount = 0;
	m_pageStartLine = 0;
	emit fileChanged();
}

void FileBrowserService::openRoot(const QString& path)
{
	const QFileInfo info(path);
	if (!info.exists() || !info.isDir())
		return;
	m_rootPath = QDir::cleanPath(info.absoluteFilePath());
	m_currentPath = m_rootPath;
	m_entries.clear();
	m_error.clear();
	clearFile();
	emit rootPathChanged();
	emit currentPathChanged();
	refresh();
}

void FileBrowserService::navigate(const QString& path)
{
	const QFileInfo info(path.trimmed());
	if (!info.exists() || !info.isDir()) {
		setError(QStringLiteral("目录不存在"));
		return;
	}
	// 地址栏允许访问本机任意目录；rootPath 只用作初始工作区，不是访问边界。
	m_currentPath = QDir::cleanPath(info.absoluteFilePath());
	setError(QString());
	clearFile();
	emit currentPathChanged();
	refresh();
}

void FileBrowserService::goUp()
{
	if (m_currentPath.isEmpty())
		return;
	const QString parent = QFileInfo(m_currentPath).absolutePath();
	if (parent == m_currentPath || parent.isEmpty())
		return;
	navigate(parent);
}

void FileBrowserService::refresh()
{
	QDir dir(m_currentPath);
	if (!dir.exists()) {
		setError(QStringLiteral("目录不存在"));
		return;
	}
	const QFileInfoList files = dir.entryInfoList(
		QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
	QVariantList rows;
	rows.reserve(files.size());
	for (const QFileInfo& info : files) {
		if (info.isHidden() && !info.isDir())
			continue;
		QVariantMap row;
		row.insert(QStringLiteral("name"), info.fileName());
		row.insert(QStringLiteral("path"), QDir::cleanPath(info.absoluteFilePath()));
		row.insert(QStringLiteral("isDir"), info.isDir());
		row.insert(QStringLiteral("sizeText"), info.isDir() ? QString() : humanSize(info.size()));
		rows.append(row);
	}
	m_entries = rows;
	setError(QString());
	emit entriesChanged();
}

bool FileBrowserService::buildLineIndex(const QString& path)
{
	m_lineOffsets.clear();
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		setError(QStringLiteral("无法读取文件"));
		return false;
	}

	// 二进制采样：只看前 64 KB，不再整文件遍历。
	const QByteArray sample = file.read(64 * 1024);
	int control = 0;
	for (char c : sample) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (u == 0) {
			setError(QStringLiteral("这是二进制文件，暂不支持预览"));
			return false;
		}
		if (u < 0x09 || (u > 0x0D && u < 0x20))
			++control;
	}
	if (!sample.isEmpty() && control * 100 / sample.size() > 5) {
		setError(QStringLiteral("这是二进制文件，暂不支持预览"));
		return false;
	}

	file.seek(0);
	m_lineOffsets.append(0);
	qint64 offset = 0;
	constexpr qint64 kChunk = 256 * 1024;
	while (!file.atEnd()) {
		const QByteArray chunk = file.read(kChunk);
		if (chunk.isEmpty())
			break;
		for (int i = 0; i < chunk.size(); ++i) {
			if (chunk.at(i) == '\n')
				m_lineOffsets.append(offset + i + 1);
		}
		offset += chunk.size();
	}
	// 哨兵：文件末尾。最后一行没有换行时也保证 page 的 end 合法。
	if (m_lineOffsets.isEmpty() || m_lineOffsets.last() != offset)
		m_lineOffsets.append(offset);
	return true;
}

bool FileBrowserService::buildLineIndexFromBuffer(const QByteArray& data)
{
	m_lineOffsets.clear();
	m_lineOffsets.append(0);
	for (int i = 0; i < data.size(); ++i) {
		if (data.at(i) == '\n')
			m_lineOffsets.append(i + 1);
	}
	if (m_lineOffsets.isEmpty() || m_lineOffsets.last() != data.size())
		m_lineOffsets.append(data.size());
	return true;
}

void FileBrowserService::loadPage(int index)
{
	if (m_filePath.isEmpty() || m_lineOffsets.size() < 2)
		return;
	if (index < 0 || index >= m_pageCount)
		return;

	const int startLine = index * kPageLines;
	const int endLine = qMin(startLine + kPageLines, totalLines());
	const qint64 start = m_lineOffsets.value(startLine, 0);
	const qint64 end = m_lineOffsets.value(endLine, m_lineOffsets.last());

	QByteArray data;
	if (m_fromBuffer) {
		data = m_buffer.mid(static_cast<int>(start), static_cast<int>(qMax<qint64>(0, end - start)));
	} else {
		QFile file(m_filePath);
		if (!file.open(QIODevice::ReadOnly)) {
			setError(QStringLiteral("无法读取文件"));
			return;
		}
		if (!file.seek(start)) {
			setError(QStringLiteral("无法定位文件分页"));
			return;
		}
		data = file.read(qMax<qint64>(0, end - start));
	}
	m_text = QString::fromUtf8(data);
	m_pageIndex = index;
	m_pageStartLine = startLine;
	setError(QString());
	emit fileChanged();
}

void FileBrowserService::openFile(const QString& path)
{
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		setError(QStringLiteral("文件不存在"));
		return;
	}
	if (OfficeTextExtractor::isSupported(path)) {
		m_fileName = info.fileName();
		m_filePath = QDir::cleanPath(info.absoluteFilePath());
		QString extracted;
		QString error;
		if (!OfficeTextExtractor::extract(path, &extracted, &error)) {
			setError(error);
			return;
		}
		m_buffer = extracted.toUtf8();
		m_officeKind = info.suffix().toLower();
		m_officeText = extracted;
		m_fromBuffer = true;

		// 发布级路径：本机 Office/WPS 异步导出 PDF，再用 PDF.js 渲染。
		// 转换期间不阻塞 UI；失败时保留 extracted 原始文本作为降级内容。
		if (!m_tempPdfPath.isEmpty())
			QFile::remove(m_tempPdfPath);
		const QString pdfDir = QDir(QCoreApplication::applicationDirPath())
			.filePath(QStringLiteral("temp/ulinhub-office"));
		QDir().mkpath(pdfDir);
		const QString pdfPath = pdfDir + QStringLiteral("/ulinhub-office-%1.pdf")
			.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		m_tempPdfPath = pdfPath;
		m_officeConverting = true;
		m_officeError.clear();
		m_officePdfBase64.clear();
		if (!m_officeBackend.isNull()) {
			QMetaObject::invokeMethod(m_officeBackend, "convertOffice", Qt::QueuedConnection,
				Q_ARG(QString, path), Q_ARG(QString, pdfPath));
		} else {
		m_convertProc = OfficeConverter::startConversion(path, pdfPath, this);
		connect(m_convertProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
			[this, pdfPath](int exitCode, QProcess::ExitStatus status) {
				QProcess* proc = m_convertProc;
				m_convertProc = nullptr;
				m_officeConverting = false;
				if (status == QProcess::NormalExit && exitCode == 0
					&& QFileInfo::exists(pdfPath) && QFileInfo(pdfPath).size() > 0) {
					QFile pdf(pdfPath);
					if (pdf.open(QIODevice::ReadOnly)) {
						m_officePdfBase64 = QString::fromLatin1(pdf.readAll().toBase64());
						m_officeError.clear();
					} else {
						m_officeError = QStringLiteral("无法读取转换后的 PDF");
					}
				} else {
					QString detail = proc ? QString::fromLocal8Bit(proc->readAllStandardError()).trimmed() : QString();
					qWarning().noquote() << "[OfficeConvert] failed:" << detail;
					m_officeError = OfficeConverter::friendlyError(detail);
				}
				if (proc)
					proc->deleteLater();
				emit fileChanged();
			});
		}
		m_pageIndex = 0;
		m_pageStartLine = 0;
		buildLineIndexFromBuffer(m_buffer);
		m_pageCount = qMax(1, (totalLines() + kPageLines - 1) / kPageLines);
		m_textMode = true;
		loadPage(0);
		return;
	}
	if (info.size() > kMaxFileBytes) {
		setError(QStringLiteral("文件超过 64 MB，暂不支持预览"));
		return;
	}
	if (info.size() == 0) {
		setError(QStringLiteral("空文件"));
		return;
	}

	m_fileName = info.fileName();
	m_filePath = QDir::cleanPath(info.absoluteFilePath());
	m_pageIndex = 0;
	m_pageStartLine = 0;
	if (!buildLineIndex(m_filePath))
		return;
	m_pageCount = qMax(1, (totalLines() + kPageLines - 1) / kPageLines);
	m_textMode = true;
	loadPage(0);
}

QString FileBrowserService::readPage(int index) const
{
	if (m_filePath.isEmpty() || m_lineOffsets.size() < 2)
		return QString();
	if (index < 0 || index >= m_pageCount)
		return QString();
	const int startLine = index * kPageLines;
	const int endLine = qMin(startLine + kPageLines, totalLines());
	const qint64 start = m_lineOffsets.value(startLine, 0);
	const qint64 end = m_lineOffsets.value(endLine, m_lineOffsets.last());
	if (m_fromBuffer)
		return QString::fromUtf8(m_buffer.mid(static_cast<int>(start),
			static_cast<int>(qMax<qint64>(0, end - start))));
	QFile file(m_filePath);
	if (!file.open(QIODevice::ReadOnly) || !file.seek(start))
		return QString();
	return QString::fromUtf8(file.read(qMax<qint64>(0, end - start)));
}

int FileBrowserService::pageStartLineAt(int index) const
{
	return index * kPageLines;
}

int FileBrowserService::pageLineCountAt(int index) const
{
	if (index < 0 || index >= m_pageCount)
		return 0;
	const int startLine = index * kPageLines;
	return qMin(kPageLines, totalLines() - startLine);
}

void FileBrowserService::nextPage()
{
	if (canNextPage())
		loadPage(m_pageIndex + 1);
}

void FileBrowserService::prevPage()
{
	if (canPrevPage())
		loadPage(m_pageIndex - 1);
}

void FileBrowserService::closeFile()
{
	clearFile();
}
