#include "common/files/OfficeTextExtractor.h"

#include "common/util/ArchiveExtractor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QXmlStreamReader>

namespace OfficeTextExtractor
{

bool isSupported(const QString& path)
{
	const QString s = QFileInfo(path).suffix().toLower();
	return s == QLatin1String("docx") || s == QLatin1String("xlsx")
		|| s == QLatin1String("pptx");
}

bool extract(const QString& path, QString* text, QString* error)
{
	if (text)
		text->clear();

	QTemporaryDir tmp;
	if (!tmp.isValid()) {
		if (error)
			*error = QStringLiteral("无法创建临时目录");
		return false;
	}
	if (!ArchiveExtractor::extractTo(path, tmp.path(), error))
		return false;

	// OOXML：docx 正文在 word/document.xml，xlsx 共享字符串在 xl/sharedStrings.xml，
	// pptx 每页在 ppt/slides/slide*.xml。
	QStringList candidates{
		tmp.path() + QStringLiteral("/word/document.xml"),
		tmp.path() + QStringLiteral("/xl/sharedStrings.xml")
	};
	QDir slides(tmp.path() + QStringLiteral("/ppt/slides"));
	const QStringList slideFiles = slides.entryList(
		{ QStringLiteral("slide*.xml") }, QDir::Files, QDir::Name);
	for (const QString& f : slideFiles)
		candidates.append(slides.filePath(f));

	QString out;
	for (const QString& f : candidates) {
		QFile file(f);
		if (!file.open(QIODevice::ReadOnly))
			continue;
		QXmlStreamReader xml(&file);
		while (!xml.atEnd()) {
			xml.readNext();
			if (xml.isCharacters() && !xml.text().toString().trimmed().isEmpty())
				out += xml.text().toString() + QLatin1Char(' ');
		}
	}
	if (text)
		*text = out.trimmed();
	return true;
}

} // namespace OfficeTextExtractor
