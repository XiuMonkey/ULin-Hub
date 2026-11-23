// 地址栏输入的规范化：纯字符串规则，没有 QObject、不碰窗口。

#include "common/util/UrlInput.h"

#include <QByteArray>
#include <QChar>
#include <QRegularExpression>
#include <QUrl>

namespace
{
	// 猜不到就搜索时用的引擎（Bing）。
	const char kSearchPrefix[] = "https://www.bing.com/search?q=";

	// scheme 判定：字母开头，后接字母/数字/`+.-`，以冒号收尾。它比真实 URL 宽，
	// `localhost:8080`、`mailto:x@y` 都会被当作已带 scheme 而原样放行，这是既有行为。
	const QRegularExpression& schemePattern()
	{
		static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
		return pattern;
	}

	// 对齐原 JS 的域名启发式 `/^[^\s]+\.[^\s]+$/`。不能用 PCRE 的 `\s` 直接照抄：
	// 它只认 ASCII 空白，而 JS 的还含全角空格、NBSP、BOM，中文用户敲全角空格时两边结论会不同。
	bool looksLikeHost(const QString& text)
	{
		for (const QChar c : text) {
			if (c.isSpace() || c.unicode() == 0xFEFF)
				return false;
		}
		const int dot = text.indexOf(QLatin1Char('.'));
		return dot > 0 && dot < text.length() - 1;
	}

	// 对齐 encodeURIComponent：UTF-8 百分号编码，但 `!*'()` 不编码（否则 `it's` 会变成 `it%27s`）。
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
