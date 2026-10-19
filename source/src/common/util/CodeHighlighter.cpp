#include "common/util/CodeHighlighter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpressionMatch>

namespace
{
	// 兜底配色：主题包里没有 syntax* 键时用它（旧风格包 / 只改了部分键的自定义包）。
	// 这一档是必要的 —— 曾经因为 `<exe>/ui/` 里的主题包是旧副本，角色查不到色，
	// 整片代码块就退化成了纯文本（"高亮直接没了"）。宁可色偏一点，也不能没有高亮。
	struct FallbackColor
	{
		const char* role;
		const char* light;
		const char* dark;
	};

	const FallbackColor kFallbackColors[] = {
		{ "keyword", "#D6336C", "#FAA2C1" },    // --shiki-token-keyword
		{ "string", "#237032", "#69DB7C" },     // --shiki-token-string（light 加深一档，见 theme-light.json）
		{ "comment", "#868E96", "#ADB5BD" },    // --shiki-token-comment
		{ "constant", "#1C7ED6", "#4DABF7" },   // --shiki-token-constant
		{ "function", "#6741D9", "#B197FC" },   // --shiki-token-function
		{ "variable", "#E8590C", "#FFA94D" },   // --shiki-token-parameter
		{ "punct", "#495057", "#CED4DA" },      // --shiki-token-punctuation
	};

	QString fallbackColor(const QString& role, bool dark)
	{
		for (const FallbackColor& entry : kFallbackColors) {
			if (role == QLatin1String(entry.role))
				return QString::fromLatin1(dark ? entry.dark : entry.light);
		}
		return QString();
	}
} // namespace

CodeHighlighter& CodeHighlighter::instance()
{
	static CodeHighlighter highlighter;
	return highlighter;
}

bool CodeHighlighter::loadFromFile(const QString& filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	file.close();
	if (!doc.isObject())
		return false;

	QMutexLocker locker(&m_mutex);
	m_rules.clear();
	m_aliases.clear();
	clearCache();

	const QJsonObject root = doc.object();

	// 别名表：大小写不敏感（加载时统一小写），值也统一小写
	const QJsonObject aliases = root.value(QStringLiteral("aliases")).toObject();
	for (auto it = aliases.begin(); it != aliases.end(); ++it)
		m_aliases.insert(it.key().trimmed().toLower(), it.value().toString().trimmed().toLower());

	const QJsonObject languages = root.value(QStringLiteral("languages")).toObject();
	for (auto it = languages.begin(); it != languages.end(); ++it) {
		const QString language = it.key();
		const QJsonArray rulesArray = it.value().toArray();

		QVector<Rule> rules;
		for (const auto& value : rulesArray) {
			const QJsonObject ruleObj = value.toObject();
			const QString pattern = ruleObj.value(QStringLiteral("pattern")).toString();
			if (pattern.isEmpty())
				continue;

			Rule rule;
			rule.regex = QRegularExpression(pattern);
			// 新格式：只写角色，色值走主题包；旧格式（color）仍认，作为单条硬覆盖
			rule.role = ruleObj.value(QStringLiteral("role")).toString();
			rule.color = ruleObj.value(QStringLiteral("color")).toString();
			rule.bold = ruleObj.value(QStringLiteral("bold")).toBool(false);
			rule.italic = ruleObj.value(QStringLiteral("italic")).toBool(false);
			rules.append(rule);
		}

		m_rules.insert(language, rules);
	}

	return true;
}

void CodeHighlighter::clearCache()
{
	QMutexLocker locker(&m_mutex);
	m_cache.clear();
}

void CodeHighlighter::setThemeColors(const QHash<QString, QString>& colors, bool dark)
{
	QMutexLocker locker(&m_mutex);

	// keys 是主题包里的 syntaxKeyword / syntaxString / … —— 去掉 "syntax" 前缀、首字母小写，
	// 就得到规则里写的角色名（keyword / string / …）。这样主题包加一个键就多一个角色，
	// 这里不需要逐键列举。
	QHash<QString, QString> byRole;
	for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
		if (!it.key().startsWith(QLatin1String("syntax")) || it.key().size() <= 6)
			continue;
		QString role = it.key().mid(6);   // "SyntaxKeyword" 之外的写法（如 _syntaxNote）会被前缀检查挡掉
		role[0] = role.at(0).toLower();
		byRole.insert(role, it.value());
	}

	m_themeColors = byRole;
	m_dark = dark;
	// 颜色变了，旧 HTML 里烘的是旧色 ⇒ 必须清缓存（换肤立刻生效的关键）
	m_cache.clear();
}

QString CodeHighlighter::normalizeHint(const QString& language) const
{
	QString hint = language.trimmed().toLower();
	if (hint.isEmpty())
		return hint;

	// 只取第一个词：```js title="x" / ```python {1,2}
	static const QRegularExpression whitespace(QStringLiteral("\\s+"));
	const int split = hint.indexOf(whitespace);
	if (split > 0)
		hint = hint.left(split);

	// 去掉 fence 里常见的装饰字符（```json, ```'js' 、```{c}）
	static const QRegularExpression decoration(QStringLiteral("^[`'\"{]+|[`'\"}]+$"));
	hint.remove(decoration);

	const auto alias = m_aliases.constFind(hint);
	return alias == m_aliases.constEnd() ? hint : alias.value();
}

QString CodeHighlighter::highlight(const QString& language, const QString& code) const
{
	QMutexLocker locker(&m_mutex);

	// 语言提示归一：只取第一个词（fence 常写 ```js title=x）、非字母数字的尾巴去掉、转小写，
	// 再经别名表落到语法 id —— 与官方 LANG_ALIASES 的取法一致（官方也是 Map 归一后查语法）。
	const QString hint = normalizeHint(language);

	// 缓存键带上当前主题：setThemeColors 已经会清缓存，这里再兜一层 ——
	// 万一将来有人只改颜色不清缓存，也不会把亮色的 HTML 端给暗色。
	const QString key = hint + QLatin1Char('\n') + code + (m_dark ? QLatin1String("\x01d") : QLatin1String("\x01l"));
	const auto cached = m_cache.constFind(key);
	if (cached != m_cache.constEnd())
		return cached.value();

	const QString lang = hint.isEmpty() ? QStringLiteral("default") : hint;
	const QVector<Rule> rules = m_rules.value(lang, m_rules.value(QStringLiteral("default")));

	// 外层必须套 <pre>：QML 侧用 RichText 渲染，裸换行与**连续空格都会折叠** ——
	// 不套的话多行代码会挤成一行、缩进全丢（实测 lineCount=1，加 <pre> 后正常）
	QString html = QStringLiteral("<pre style=\"margin:0;font-family:Consolas,monospace;\">");
	int pos = 0;
	const int codeLength = code.length();

	// 逐位置贪心：每轮在剩余文本里找最早、同位置最长的那条规则；前段原样转义，命中段套 span
	while (pos < codeLength) {
		int bestPos = -1;
		int bestLen = 0;
		const Rule* bestRule = nullptr;

		for (const Rule& rule : rules) {
			const QRegularExpressionMatch match = rule.regex.match(code, pos);
			if (!match.hasMatch())
				continue;

			const int matchPos = match.capturedStart();
			const int matchLen = match.capturedLength();
			if (matchLen <= 0)
				continue;

			if (bestPos < 0 || matchPos < bestPos
				|| (matchPos == bestPos && matchLen > bestLen)) {
				bestPos = matchPos;
				bestLen = matchLen;
				bestRule = &rule;
			}
		}

		if (!bestRule) {
			html += code.mid(pos).toHtmlEscaped();
			break;
		}

		html += code.mid(pos, bestPos - pos).toHtmlEscaped();

		const QString matched = code.mid(bestPos, bestLen).toHtmlEscaped();
		// 色值 = 单条硬覆盖 > 主题包里该角色的色 > 内置兜底表 >（都没有）不着色。
		// 兜底表保证"主题包缺键"不会让整片代码块失去高亮（这个坑踩过一次）。
		QString color = !bestRule->color.isEmpty()
			? bestRule->color
			: m_themeColors.value(bestRule->role);
		if (color.isEmpty())
			color = fallbackColor(bestRule->role, m_dark);
		if (color.isEmpty()) {
			html += matched;
			pos = bestPos + bestLen;
			continue;
		}

		QString style = QStringLiteral("color:%1;").arg(color);
		if (bestRule->bold)
			style += QStringLiteral("font-weight:bold;");
		if (bestRule->italic)
			style += QStringLiteral("font-style:italic;");
		html += QStringLiteral("<span style=\"") + style + QStringLiteral("\">") + matched + QStringLiteral("</span>");

		pos = bestPos + bestLen;
	}

	html += QStringLiteral("</pre>");
	m_cache.insert(key, html);
	return html;
}