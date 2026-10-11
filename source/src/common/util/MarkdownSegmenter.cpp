// 见 MarkdownSegmenter.h：Markdown -> 正文/代码块的有序单元。

#include "common/util/MarkdownSegmenter.h"

#include "common/util/CodeHighlighter.h"

#include <QDebug>
#include <QStringList>
#include <QVariantMap>

namespace
{
	// 内置高亮规则在 qrc 里的位置（见 assets/resources/DSHHub.qrc），外部覆盖优先
	const char kRulesResource[] = ":/DSHHub/highlight_rules.json";
	QString g_rulesPath;
	bool g_rulesLoaded = false;

	bool isFence(const QString& line, QString* language)
	{
		const QString trimmed = line.trimmed();
		if (!trimmed.startsWith(QLatin1String("```")))
			return false;

		if (language)
			*language = trimmed.mid(3).trimmed();
		return true;
	}

	QString escapeHtml(const QString& text)
	{
		return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
	}

	void flushProse(QVariantList& parts, QStringList& buffer)
	{
		if (buffer.isEmpty())
			return;

		// 前后各去掉空行：可编辑内容里围栏前后常带空行，留着会在气泡里多出一段空白
		while (!buffer.isEmpty() && buffer.first().trimmed().isEmpty())
			buffer.removeFirst();
		while (!buffer.isEmpty() && buffer.last().trimmed().isEmpty())
			buffer.removeLast();

		if (!buffer.isEmpty()) {
			QVariantMap part;
			part.insert(QStringLiteral("kind"), QStringLiteral("prose"));
			part.insert(QStringLiteral("text"), buffer.join(QLatin1Char('\n')));
			parts.append(part);
		}
		buffer.clear();
	}
}

namespace MarkdownSegmenter
{
	void ensureHighlightRulesLoaded()
	{
		if (g_rulesLoaded)
			return;

		g_rulesLoaded = true;
		if (CodeHighlighter::instance().loadFromFile(QString::fromLatin1(kRulesResource)))
			return;

		qWarning().noquote() << "[Markdown] 高亮规则装载失败，代码块退化为纯文本："
			<< QString::fromLatin1(kRulesResource);
	}

	QVariantList split(const QString& markdown)
	{
		QVariantList parts;
		if (markdown.isEmpty())
			return parts;

		const QStringList lines = markdown.split(QLatin1Char('\n'));
		QStringList proseBuffer;
		QString codeBuffer;
		QString language;
		bool inCode = false;

		for (const QString& line : lines) {
			QString fenceLanguage;
			const bool fence = isFence(line, &fenceLanguage);

			if (!inCode) {
				if (fence) {
					flushProse(parts, proseBuffer);
					inCode = true;
					language = fenceLanguage;
					codeBuffer.clear();
				}
				else {
					proseBuffer.append(line);
				}
				continue;
			}

			// 代码块内：遇到围栏就收尾（空语言也认，```` ``` ```` 是标准收尾写法）
			if (fence) {
				QVariantMap part;
				part.insert(QStringLiteral("kind"), QStringLiteral("code"));
				part.insert(QStringLiteral("language"), language);
				part.insert(QStringLiteral("text"), codeBuffer);

				const QString highlighted = CodeHighlighter::instance().highlight(language, codeBuffer);
				part.insert(QStringLiteral("html"),
					highlighted.isEmpty() ? escapeHtml(codeBuffer) : highlighted);
				parts.append(part);

				inCode = false;
				language.clear();
				codeBuffer.clear();
				continue;
			}

			if (!codeBuffer.isEmpty())
				codeBuffer.append(QLatin1Char('\n'));
			codeBuffer.append(line);
		}

		// 收尾：未闭合的围栏按代码块处理（流式过程中很常见）
		if (inCode) {
			QVariantMap part;
			part.insert(QStringLiteral("kind"), QStringLiteral("code"));
			part.insert(QStringLiteral("language"), language);
			part.insert(QStringLiteral("text"), codeBuffer);
			const QString highlighted = CodeHighlighter::instance().highlight(language, codeBuffer);
			part.insert(QStringLiteral("html"),
				highlighted.isEmpty() ? escapeHtml(codeBuffer) : highlighted);
			parts.append(part);
		}
		else {
			flushProse(parts, proseBuffer);
		}

		return parts;
	}
}
