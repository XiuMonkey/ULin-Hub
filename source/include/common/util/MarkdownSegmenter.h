#pragma once

// 把 Markdown 按"代码围栏"切成有序单元 —— 这是旧壳 AgentMessageUnit 的既定规则：
// 同一气泡里正文与代码块严格按出现顺序排布，代码围栏切成**独立单元**（带语言标签、不换行、自带滚动），
// 而不是整段塞进一个富文本控件。规则本身不变，只是渲染从 QTextBrowser 换成 QML 原生控件。
//
// 产出单元（QVariantMap）：
//   { kind: "prose", text }                                   —— 交给 Text{ textFormat: MarkdownText }
//   { kind: "code", language, text, html }                     —— 交给 CodeBlock.qml（html 已高亮）

#include <QString>
#include <QVariantList>

namespace MarkdownSegmenter
{
	// 启动期调一次：装载语法高亮规则（qrc 里的 highlight-rules.json）。
	// 规则缺失不报错，只是 code 单元的 html 退化为转义纯文本。
	void ensureHighlightRulesLoaded();

	QVariantList split(const QString& markdown);
}
