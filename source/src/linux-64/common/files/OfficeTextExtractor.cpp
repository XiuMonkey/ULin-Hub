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
	// 只认 OOXML 三兄弟：docx / xlsx / pptx
	const QString s = QFileInfo(path).suffix().toLower();
	return s == QLatin1String("docx") || s == QLatin1String("xlsx")
		|| s == QLatin1String("pptx");
}

bool extract(const QString& path, QString* text, QString* error)
{
	if (text)
		text->clear();

	// 解到临时目录（QTemporaryDir 析构时自动清掉），路径穿越等安全检查在 ArchiveExtractor 里
	QTemporaryDir tmp;
	if (!tmp.isValid()) {
		if (error)
			*error = QStringLiteral("无法创建临时目录");
		return false;
	}
	if (!ArchiveExtractor::extractTo(path, tmp.path(), error))
		return false;

	// OOXML：docx 正文在 word/document.xml，xlsx 共享字符串在 xl/sharedStrings.xml，
	// pptx 每页在 ppt/slides/slide*.xml。三条都解不出正文时就是空字符串，不算失败。
	QStringList candidates{
		tmp.path() + QStringLiteral("/word/document.xml"),
		tmp.path() + QStringLiteral("/xl/sharedStrings.xml")
	};
	QDir slides(tmp.path() + QStringLiteral("/ppt/slides"));
	// 按文件名排序，页序才是 slide1、slide2…（entryList 的原始顺序不保证）
	const QStringList slideFiles = slides.entryList({ QStringLiteral("slide*.xml") }, QDir::Files, QDir::Name);
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
			// 只取字符数据（w:t / a:t 里的正文），标签与属性一概不要；段间用空格分开
			if (xml.isCharacters() && !xml.text().toString().trimmed().isEmpty())
				out += xml.text().toString() + QLatin1Char(' ');
		}
	}
	if (text)
		*text = out.trimmed();
	// 一个正文都没读到也返回 true（text 为空串）：这里只区分"解包失败"，内容为空由调用方自己判断
	return true;
}

} // namespace OfficeTextExtractor
