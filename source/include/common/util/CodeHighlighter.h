#pragma once

// 按 highlight-rules.json 的规则给代码块做语法高亮；初始化时一次性加载规则并缓存结果。

#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QString>
#include <QVector>

class CodeHighlighter
{
public:
	static CodeHighlighter& instance();

	// 从 JSON 文件加载高亮规则；重复调用会先清空旧规则
	bool loadFromFile(const QString& filePath);

	// 对代码进行高亮，返回可直接交给 RichText（Text/TextEdit）的 HTML；
	// 返回值自带 <pre> 包裹，否则调用方的 RichText 会把换行与缩进折叠掉
	QString highlight(const QString& language, const QString& code) const;

	// 主题色（亮/暗各一次）：规则里只写"角色"，色值由主题包给 —— 见 theme-*.json 的 syntax* 键。
	// 每次调用都会清缓存（颜色变了，旧 HTML 里烘的是旧色）；由 ThemeBridge 在装载/切换主题时调。
	void setThemeColors(const QHash<QString, QString>& colors, bool dark);

	void clearCache();

private:
	CodeHighlighter() = default;

	// fence 标签 -> 语法 id：取第一个词、去掉装饰字符、转小写，再查别名表（查不到就原样返回）
	QString normalizeHint(const QString& language) const;

	struct Rule
	{
		QRegularExpression regex;
		// role = 语义角色（keyword/string/comment/constant/function/variable/punct），
		// 由主题包映射成具体色值；color 非空时作为单条规则的硬覆盖（一般不用）。
		QString role;
		QString color;
		bool bold = false;
		bool italic = false;
	};

	QHash<QString, QVector<Rule>> m_rules;
	// 别名表（"py" -> "python"、"js" -> "typescript"、"c++" -> "cpp" ...）：
	// fence 标签是模型随手写的，官方 DSH 也是先用 LANG_ALIASES 归一再用语法。
	QHash<QString, QString> m_aliases;
	// 当前主题的"角色 -> 色值"（syntaxKeyword 等 7 个键）；由 setThemeColors 填。
	QHash<QString, QString> m_themeColors;
	bool m_dark = false;
	mutable QHash<QString, QString> m_cache;
	// 高亮会被渲染 worker 线程并发调用，规则/缓存读写统一加锁；用递归锁是因为
	// loadFromFile 持锁期间会调用同样加锁的 clearCache()。
	mutable QRecursiveMutex m_mutex;
};
