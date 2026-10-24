// 见 UrlInput.h：地址栏输入的规范化（纯字符串规则，没有 QObject、不碰窗口）。

#include "common/util/UrlInput.h"

#include <QByteArray>
#include <QChar>
#include <QRegularExpression>
#include <QUrl>

namespace
{
	// 搜索引擎：保持一致（Bing）。改这里就等于改地址栏的"猜不到就搜"行为。
	const char kSearchPrefix[] = "https://www.bing.com/search?q=";

	// 后面是字母/数字/+ . -，以冒号收尾）。注意它比"真的是 URL"宽：`localhost:8080`、
	// `mailto:x@y` 都会被认成"已经带了 scheme"而**原样放行** —— 这是既有行为，别收紧。
	const QRegularExpression& schemePattern()
	{
		static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
		return pattern;
	}

	// 原 JS 的域名启发式 `/^[^\s]+\.[^\s]+$/`：整串无空白，且第一个点既不在开头也不在结尾。
	// ⚠️ 不能直接用 PCRE 的 `\s` 去抄这条正则：PCRE 默认的 `\s` 只认 ASCII 空白，而 JS 的 `\s`
	// 还包含全角空格 U+3000、NBSP、各类 Unicode 空格与 BOM —— 中文用户很容易敲出全角空格，
	// 两边结论就会不一样（"例子　测试"在 JS 里该去搜索，用 PCRE `\s` 会被拼成域名）。
	bool looksLikeHost(const QString& text)
	{
		for (const QChar c : text) {
			if (c.isSpace() || c.unicode() == 0xFEFF)
				return false;
		}
		const int dot = text.indexOf(QLatin1Char('.'));
		return dot > 0 && dot < text.length() - 1;
	}

	// 原 JS 用的是 encodeURIComponent：UTF-8 百分号编码，且 `!*'()` 这几个**不**编码。
	// 否则 "it's" 会变 "it%27s" 而是 "it's"。
	QString percentEncode(const QString& text)
	{
		return QString::fromLatin1(QUrl::toPercentEncoding(text, QByteArray("!*'()")));
	}
}

QString normalizeUrlInput(const QString& raw)
{
	const QString trimmed = raw.trimmed();
	if (trimmed.isEmpty())
		return QString();

	if (schemePattern().match(trimmed).hasMatch())
		return trimmed;

	if (looksLikeHost(trimmed))
		return QStringLiteral("https://") + percentEncode(trimmed);

	return QLatin1String(kSearchPrefix) + percentEncode(trimmed);
}
