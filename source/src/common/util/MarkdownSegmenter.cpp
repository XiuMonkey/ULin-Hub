// 见 MarkdownSegmenter.h：
// ① split() 按代码围栏切 prose/code —— 代码块继续交给 CodeBlock.qml；
// ② renderFragment() 用 Qt 6 内置 md4c 解析 GitHub 方言，再按 DSH 的块级规格调整 QTextDocument；
// ③ QML 的 MarkdownBody 直接持有 QTextDocument 并增量插入 fragment。
// 这里刻意不引入第三方 md4c 源码：Qt 6 的 QTextDocument::setMarkdown() 已经内置 md4c，
// 这样不需要改 vcxproj，也不会多出一份 C 依赖。

#include "common/util/MarkdownSegmenter.h"

#include "common/util/CodeHighlighter.h"

#include <QColor>
#include <QDebug>
#include <QFont>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QVariantMap>

#include <functional>

namespace
{
	const char kRulesResource[] = ":/ULinHub/config/highlight-rules.json";   // 内置高亮规则在 qrc 里的位置（见 assets/resources/ulinhub.qrc），外部覆盖优先
	bool g_rulesLoaded = false;

	QColor colorOf(const QVariantMap& colors, const char* key, const QColor& fallback)
	{
		const QVariant value = colors.value(QString::fromLatin1(key));
		if (!value.isValid())
			return fallback;
		const QColor color = value.value<QColor>();
		return color.isValid() ? color : fallback;
	}

	void styleTable(QTextTable* table, const QColor& text)
	{
		if (table == nullptr)
			return;

		// QML 的 QQuickTextEdit 不画 cell border；表格线由 MarkdownBody 的 Rectangle 覆盖层
		// 按 MarkdownBridge 给出的行坐标绘制，所以这里文档内不能画 grid。
		QTextTableFormat format = table->format();
		format.setBorder(0);
		format.setBorderStyle(QTextFrameFormat::BorderStyle_None);
		format.setBorderCollapse(true);
		format.setCellSpacing(0);
		format.setCellPadding(0);
		format.setBackground(Qt::transparent);

		// 旧 MarkdownBody 在块与块之间给 table 留 16px 的间距；单文档 margin 不折叠，
		// 所以 table 底部固定 16px、顶部 8px（与相邻段落 8px 合计 16px），不再继续加大。
		format.setTopMargin(8);
		format.setBottomMargin(16);
		format.setLeftMargin(0);
		format.setRightMargin(0);
		format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
		table->setFormat(format);

		const int columns = table->columns();
		for (int row = 0; row < table->rows(); ++row) {
			for (int column = 0; column < columns; ++column) {
				QTextTableCell cell = table->cellAt(row, column);
				if (!cell.isValid())
					continue;

				QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
				cellFormat.setBackground(Qt::transparent);
				cellFormat.setTopBorder(0);
				cellFormat.setLeftBorder(0);
				cellFormat.setRightBorder(0);
				cellFormat.setBottomBorder(0);
				cellFormat.setTopPadding(10);
				cellFormat.setBottomPadding(10);
				cellFormat.setLeftPadding(column == 0 ? 0 : 16);
				cellFormat.setRightPadding(column == columns - 1 ? 0 : 16);
				cell.setFormat(cellFormat);

				QTextCursor blockCursor = cell.firstCursorPosition();
				const int cellEnd = cell.lastCursorPosition().position();
				while (blockCursor.position() <= cellEnd) {
					QTextBlockFormat cellBlock = blockCursor.blockFormat();
					cellBlock.setTopMargin(0);
					cellBlock.setBottomMargin(0);
					cellBlock.setLineHeight(22, QTextBlockFormat::FixedHeight);
					blockCursor.setBlockFormat(cellBlock);
					if (!blockCursor.movePosition(QTextCursor::NextBlock))
						break;
				}

				QTextCursor contentCursor = cell.firstCursorPosition();
				contentCursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
				QTextCharFormat charFormat;
				charFormat.setForeground(text);
				charFormat.setFontWeight(row == 0 ? QFont::Medium : QFont::Normal);
				contentCursor.mergeCharFormat(charFormat);
			}
		}
	}

	void styleDocument(QTextDocument& document, const QVariantMap& colors)
	{
		const QColor text = colorOf(colors, "text", QColor("#E5E5E5"));
		const QColor secondary = colorOf(colors, "secondary", QColor("#B8B8B8"));
		const QColor inlineBg = colorOf(colors, "inlineBg", QColor("#2A2A2E"));
		const QColor link = colorOf(colors, "link", QColor("#B8B8B8"));

		QFont base(QStringLiteral("Microsoft YaHei UI"));
		base.setPixelSize(13);
		document.setDefaultFont(base);
		document.setDocumentMargin(0);

		int blockIndex = 0;
		const int blockCount = document.blockCount();
		for (QTextBlock block = document.begin(); block.isValid(); block = block.next(), ++blockIndex) {
			QTextBlockFormat blockFormat = block.blockFormat();
			QTextCursor cursor(block);

			const int heading = blockFormat.headingLevel();
			const bool listItem = block.textList() != nullptr;
			const bool quote = !listItem && heading == 0
				&& blockFormat.leftMargin() >= 30 && blockFormat.rightMargin() >= 30;
			const bool first = blockIndex == 0;
			const bool last = blockIndex == blockCount - 1;

			if (heading > 0) {
				// 对齐旧 MarkdownHeading：h1 20/29 · h2 18/27 · h3 17/25 · h4-h6 13/23（正文 13px）
				static const int kHeadingSizes[] = { 0, 20, 18, 17, 13, 13, 13 };
				static const int kHeadingLineBoxes[] = { 0, 29, 27, 25, 23, 23, 23 };
				const int level = qBound(1, heading, 6);
				const int size = kHeadingSizes[level];
				const int lineBox = kHeadingLineBoxes[level];

				blockFormat.setLineHeight(lineBox, QTextBlockFormat::FixedHeight);

				// QTextDocument 的块 margin 不折叠；用 24/8 与相邻段落 8px 相加，
				// 近似旧 MarkdownBody 的 h1–h3 32/16、h4–h6 16/16。
				blockFormat.setTopMargin(first ? 0 : (level <= 3 ? 24 : 8));
				blockFormat.setBottomMargin(last ? 0 : 8);

				QTextCharFormat headingFormat;
				headingFormat.setProperty(QTextFormat::FontPixelSize, size);
				headingFormat.setFontWeight(level <= 3 ? QFont::Bold : QFont::DemiBold);
				headingFormat.setForeground(text);
				cursor.select(QTextCursor::BlockUnderCursor);
				cursor.mergeCharFormat(headingFormat);
			}
			else {
				blockFormat.setLineHeight(23, QTextBlockFormat::FixedHeight);   // 旧 MarkdownBody：段落/表格之间 16px；单文档里相邻 margin 不折叠，所以各给 8
				blockFormat.setTopMargin(first ? 0 : (listItem ? 2 : 8));
				blockFormat.setBottomMargin(last ? 0 : (listItem ? 2 : 8));
				if (quote) {
					// QTextBlockFormat 没有 border-left；旧 MarkdownQuote 是 16px 缩进 + 2px 左边线。
					// 这里保留缩进和次要文字色，左边线只能等以后自定义 layout 再补。
					blockFormat.setLeftMargin(16);
					blockFormat.setRightMargin(0);
				}
			}

			cursor.setBlockFormat(blockFormat);

			if (listItem) {
				QTextListFormat listFormat = block.textList()->format();
				listFormat.setIndent(1);
				block.textList()->setFormat(listFormat);
			}

			// 行内 code / 链接：Qt 的 markdown importer 只给 monospace，没有 DSH 的底色和链接色。
			for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
				const QTextFragment fragment = it.fragment();
				if (!fragment.isValid())
					continue;
				const QTextCharFormat source = fragment.charFormat();
				QTextCharFormat patch;
				bool changed = false;

				if (!source.anchorHref().isEmpty()) {
					patch.setForeground(link);
					patch.setFontUnderline(true);
					changed = true;
				}
				else if (source.fontFixedPitch()) {
					patch.setFontFamily(QStringLiteral("Cascadia Mono"));
					patch.setFontFixedPitch(true);
					patch.setProperty(QTextFormat::FontPixelSize, 11);
					patch.setBackground(inlineBg);
					patch.setForeground(text);
					changed = true;
				}
				else if (source.foreground().style() == Qt::NoBrush) {
					patch.setForeground(quote ? secondary : text);
					changed = true;
				}

				if (!changed)
					continue;

				QTextCursor fragmentCursor(&document);
				fragmentCursor.setPosition(fragment.position());
				fragmentCursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
				fragmentCursor.mergeCharFormat(patch);
			}
		}

		std::function<void(QTextFrame*)> walk = [&](QTextFrame* frame) {   // 表格：QTextDocument 的 markdown importer 已经建好 QTextTable，这里只改边框/表头
			if (frame == nullptr)
				return;
			for (QTextFrame* child : frame->childFrames()) {
				if (QTextTable* table = dynamic_cast<QTextTable*>(child))
					styleTable(table, text);
				walk(child);
			}
		};
		walk(document.rootFrame());
	}

	QString escapeHtml(const QString& text)
	{
		return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
	}

	bool isFence(const QString& line, QString* language)
	{
		const QString trimmed = line.trimmed();
		if (!trimmed.startsWith(QLatin1String("```")))
			return false;

		if (language)
			*language = trimmed.mid(3).trimmed();
		return true;
	}

	void flushProse(QVariantList& parts, QStringList& buffer)
	{
		if (buffer.isEmpty())
			return;

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
		// 代码块仍交给 CodeBlock.qml：这里保留旧的围栏切分规则。
		// 代价：拖选不能跨过代码块（不同 QML 控件）；同一条回复里的代码块外观与复制按钮不变。
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

	QTextDocumentFragment MarkdownSegmenter::renderFragment(const QString& markdown, const QVariantMap& colors)
	{
		if (markdown.isEmpty())
			return QTextDocumentFragment();

		QTextDocument document;
		document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
		styleDocument(document, colors);
		QTextCursor cursor(&document);
		cursor.select(QTextCursor::Document);
		return QTextDocumentFragment(cursor);
	}
}
