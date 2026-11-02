#pragma once

// C++ 侧 Markdown 解析/排版：
//  ① 用 Qt 6 内置的 md4c（GitHub 方言）解析成 QTextDocument；
//  ② 按 DSH 的排版规格改标题/列表/引用/代码块/表格的格式；
//  ③ QML 的 MarkdownBody 直接持有这个 QTextDocument，增量插入 fragment。
// split 保留旧规则：正文按代码围栏切成 prose/code 单元，代码块继续交给 CodeBlock.qml。
// 因此拖选能跨段落/表格，但不能跨进代码块（不同 QML 控件）。
// 产出单元（QVariantMap）：
//  { kind: "prose", text } —— 交给 MarkdownBody.qml
//  { kind: "code", language, text, html } —— 交给 CodeBlock.qml
// renderFragment 的 colors 键：text / secondary / link。

#include <QString>
#include <QTextDocumentFragment>
#include <QVariantList>
#include <QVariantMap>

namespace MarkdownSegmenter
{
	// 启动期调一次：装载语法高亮规则（qrc 里的 highlight-rules.json）。
	// 规则缺失不报错；C++ QTextDocument 渲染不依赖它，保留给旧 CodeBlock 调用点。
	void ensureHighlightRulesLoaded();

	QVariantList split(const QString& markdown);

	// 增量流式用：把一小段 Markdown 排成可插入 QTextDocument 的 fragment。
	// 流式 insertFragment 后重新压一遍块级间距（标题下边距等不会被 fragment 插入稳定保留）。
	void restyleBlocks(QTextDocument& document, const QVariantMap& colors);

	QTextDocumentFragment renderFragment(const QString& markdown, const QVariantMap& colors);
}
