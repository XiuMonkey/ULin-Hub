#include "common/files/OfficeTextExtractor.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStringList>
#include <QUuid>
#include <QXmlStreamReader>

namespace
{
QString suffixOf(const QString& path)
{
	return QFileInfo(path).suffix().toLower();
}

bool unzipTo(const QString& archive, const QString& target, QString* error)
{
	QProcess proc;
#ifdef Q_OS_WIN
	proc.setProgram(QStringLiteral("tar.exe"));
#else
	proc.setProgram(QStringLiteral("tar"));
#endif
	proc.setArguments({ QStringLiteral("-xf"), QDir::toNativeSeparators(archive),
		QStringLiteral("-C"), QDir::toNativeSeparators(target) });
	proc.start();
	if (!proc.waitForStarted(5000)) {
		if (error)
			*error = QStringLiteral("无法启动解包工具");
		return false;
	}
	if (!proc.waitForFinished(20000)) {
		proc.kill();
		if (error)
			*error = QStringLiteral("解包超时");
		return false;
	}
	if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
		if (error)
			*error = QStringLiteral("解包失败：%1").arg(
				QString::fromLocal8Bit(proc.readAllStandardError()).left(200));
		return false;
	}
	return true;
}

QString readFileText(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QString();
	return QString::fromUtf8(file.readAll());
}

QStringList sortedFiles(const QString& dirPath, const QString& nameFilter)
{
	QDir dir(dirPath);
	const QFileInfoList files = dir.entryInfoList({ nameFilter }, QDir::Files, QDir::Name);
	QStringList result;
	result.reserve(files.size());
	for (const QFileInfo& info : files)
		result.append(info.absoluteFilePath());
	return result;
}

QString docxText(const QString& documentXml)
{
	QXmlStreamReader xml(documentXml);
	QString text;
	while (!xml.atEnd()) {
		xml.readNext();
		if (!xml.isStartElement())
			continue;
		const QString name = xml.name().toString();
		if (name == QLatin1String("t")) {
			text += xml.readElementText(QXmlStreamReader::IncludeChildElements);
		} else if (name == QLatin1String("tab")) {
			text += QLatin1Char('\t');
		} else if (name == QLatin1String("br")) {
			text += QLatin1Char('\n');
		} else if (name == QLatin1String("p")) {
			if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
				text += QLatin1Char('\n');
		} else if (name == QLatin1String("tr")) {
			if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
				text += QLatin1Char('\n');
		} else if (name == QLatin1String("tc")) {
			if (!text.isEmpty() && !text.endsWith(QLatin1Char('\t')))
				text += QLatin1Char('\t');
		}
	}
	return text.trimmed();
}

int columnIndex(const QString& ref)
{
	int col = 0;
	for (QChar c : ref) {
		if (c.isLetter())
			col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
		else
			break;
	}
	return qMax(0, col - 1);
}

int rowIndex(const QString& ref)
{
	int row = 0;
	for (QChar c : ref) {
		if (c.isDigit())
			row = row * 10 + (c.unicode() - '0');
	}
	return qMax(0, row - 1);
}

QStringList workbookSheetNames(const QString& workbookXml)
{
	QStringList names;
	QXmlStreamReader xml(workbookXml);
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement() && xml.name().toString() == QLatin1String("sheet")) {
			const QString name = xml.attributes().value(QStringLiteral("name")).toString();
			if (!name.isEmpty())
				names.append(name);
		}
	}
	return names;
}

QString xlsxSheetText(const QString& sheetXml, const QStringList& shared)
{
	QMap<int, QMap<int, QString>> rows;
	QXmlStreamReader xml(sheetXml);
	int row = -1;
	int col = -1;
	bool inCell = false;
	bool cellIsShared = false;
	QString cellRef;
	QString value;
	QString inlineText;

	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement()) {
			const QString name = xml.name().toString();
			if (name == QLatin1String("c")) {
				inCell = true;
				cellRef = xml.attributes().value(QStringLiteral("r")).toString();
				cellIsShared = xml.attributes().value(QStringLiteral("t")).toString() == QLatin1String("s");
				row = rowIndex(cellRef);
				col = columnIndex(cellRef);
				value.clear();
				inlineText.clear();
			} else if (name == QLatin1String("v") && inCell) {
				value = xml.readElementText(QXmlStreamReader::IncludeChildElements);
			} else if (name == QLatin1String("t") && inCell) {
				inlineText = xml.readElementText(QXmlStreamReader::IncludeChildElements);
			}
		} else if (xml.isEndElement() && xml.name().toString() == QLatin1String("c")) {
			QString cell = inlineText;
			if (cell.isEmpty() && !value.isEmpty()) {
				if (cellIsShared) {
					bool ok = false;
					const int idx = value.toInt(&ok);
					if (ok && idx >= 0 && idx < shared.size())
						cell = shared.at(idx);
				} else {
					cell = value;
				}
			}
			if (row >= 0 && col >= 0 && !cell.isEmpty())
				rows[row][col] = cell;
			inCell = false;
		}
	}

	QString out;
	for (auto rowIt = rows.constBegin(); rowIt != rows.constEnd(); ++rowIt) {
		const QMap<int, QString>& cells = rowIt.value();
		if (cells.isEmpty())
			continue;
		int maxCol = 0;
		for (auto it = cells.constBegin(); it != cells.constEnd(); ++it)
			maxCol = qMax(maxCol, it.key());
		QStringList line;
		for (int c = 0; c <= maxCol; ++c)
			line.append(cells.value(c, QString()));
		out += line.join(QStringLiteral("  |  "));
		out += QLatin1Char('\n');
	}
	return out.trimmed();
}

QString xlsxText(const QString& root)
{
	const QString sharedXml = readFileText(root + QStringLiteral("/xl/sharedStrings.xml"));
	QStringList shared;
	if (!sharedXml.isEmpty()) {
		QXmlStreamReader xml(sharedXml);
		QString current;
		while (!xml.atEnd()) {
			xml.readNext();
			if (xml.isStartElement() && xml.name().toString() == QLatin1String("si")) {
				current.clear();
			} else if (xml.isStartElement() && xml.name().toString() == QLatin1String("t")) {
				current += xml.readElementText(QXmlStreamReader::IncludeChildElements);
			} else if (xml.isEndElement() && xml.name().toString() == QLatin1String("si")) {
				shared.append(current);
			}
		}
	}

	const QStringList names = workbookSheetNames(root + QStringLiteral("/xl/workbook.xml"));
	const QStringList sheets = sortedFiles(root + QStringLiteral("/xl/worksheets"), QStringLiteral("*.xml"));
	QString out;
	for (int i = 0; i < sheets.size(); ++i) {
		const QString title = i < names.size() ? names.at(i) : QStringLiteral("Sheet %1").arg(i + 1);
		out += QStringLiteral("## %1\n").arg(title);
		out += xlsxSheetText(readFileText(sheets.at(i)), shared);
		out += QStringLiteral("\n\n");
	}
	return out.trimmed();
}

QString pptxText(const QString& root)
{
	const QStringList slides = sortedFiles(root + QStringLiteral("/ppt/slides"), QStringLiteral("slide*.xml"));
	QString out;
	for (int i = 0; i < slides.size(); ++i) {
		out += QStringLiteral("## Slide %1\n").arg(i + 1);
		QString text;
		QXmlStreamReader xml(readFileText(slides.at(i)));
		while (!xml.atEnd()) {
			xml.readNext();
			if (xml.isStartElement() && xml.name().toString() == QLatin1String("t")) {
				text += xml.readElementText(QXmlStreamReader::IncludeChildElements);
			} else if (xml.isStartElement() && xml.name().toString() == QLatin1String("p")) {
				if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
					text += QLatin1Char('\n');
			}
		}
		out += text.trimmed();
		out += QStringLiteral("\n\n");
	}
	return out.trimmed();
}
} // namespace

namespace OfficeTextExtractor
{
bool isSupported(const QString& path)
{
	const QString suffix = suffixOf(path);
	return suffix == QLatin1String("docx") || suffix == QLatin1String("xlsx")
		|| suffix == QLatin1String("pptx");
}

bool extract(const QString& path, QString* text, QString* error)
{
	if (text)
		text->clear();
	const QString suffix = suffixOf(path);
	if (!isSupported(path)) {
		if (error)
			*error = QStringLiteral("不支持的办公文件格式");
		return false;
	}

	// 不要用系统 TEMP：宿主可能以受限/提权方式运行，系统临时目录未必可写。
	const QString tempPath = QDir(QCoreApplication::applicationDirPath())
		.filePath(QStringLiteral("temp/ulinhub-office/extract-")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces));
	if (!QDir().mkpath(tempPath)) {
		if (error)
			*error = QStringLiteral("无法创建临时目录：") + tempPath;
		return false;
	}
	const auto tempGuard = qScopeGuard([tempPath]() { QDir(tempPath).removeRecursively(); });
	Q_UNUSED(tempGuard)
	if (!unzipTo(path, tempPath, error))
		return false;

	QString result;
	if (suffix == QLatin1String("docx"))
		result = docxText(readFileText(tempPath + QStringLiteral("/word/document.xml")));
	else if (suffix == QLatin1String("xlsx"))
		result = xlsxText(tempPath);
	else if (suffix == QLatin1String("pptx"))
		result = pptxText(tempPath);

	if (result.trimmed().isEmpty()) {
		if (error)
			*error = QStringLiteral("没有提取到可显示的文本内容");
		return false;
	}
	if (text)
		*text = result;
	return true;
}
} // namespace OfficeTextExtractor
