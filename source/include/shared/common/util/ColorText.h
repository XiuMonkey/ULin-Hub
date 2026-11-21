#pragma once

// 颜色文本解析：theme-*.json 里的颜色可能是 `#RRGGBB` / `#AARRGGBB` / SVG 颜色名，
// 也可能是 `rgb()` / `rgba()` —— 后者 QColor 自己不认，这里补上。
//
// 原先 CardShadow::parseColor（无 ok，失败返回无效 QColor）与 ThemeBridge::parsePaletteColor
// （有 ok，失败返回品红占位）各有一份，正则几乎相同但能力不同（前者支持百分比 alpha）。
// 合并成一份后两者语义靠返回值区分：只关心"能不能解析"就传 ok，想要具体颜色再看 isValid()。

#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QtGlobal>

// 解析失败返回无效 QColor（isValid() == false）；ok 可空。
inline QColor parseColorText(const QString& text, bool* ok = nullptr)
{
	if (ok)
		*ok = false;

	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return QColor();

	static const QRegularExpression fn(QStringLiteral(
		"^rgba?\\(\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*"
		"(?:,\\s*([0-9]*\\.?[0-9]+)\\s*(%)?\\s*)?\\)$"),
		QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch m = fn.match(trimmed);
	if (m.hasMatch()) {
		QColor color(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
		const QString alphaText = m.captured(4);
		if (!alphaText.isEmpty()) {
			const double value = alphaText.toDouble();
			int alpha = 255;
			if (!m.captured(5).isEmpty())
				alpha = qRound(value * 255.0 / 100.0);   // 50% -> 128
			else if (value <= 1.0)
				alpha = qRound(value * 255.0);           // 0.5 -> 128
			else
				alpha = qRound(value);                   // 128 -> 128
			color.setAlpha(qBound(0, alpha, 255));
		}
		if (ok)
			*ok = true;
		return color;
	}

	const QColor direct(trimmed);   // #RRGGBB / #AARRGGBB / 颜色名 / transparent
	if (direct.isValid()) {
		if (ok)
			*ok = true;
		return direct;
	}
	return QColor();
}
