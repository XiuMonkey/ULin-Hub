#pragma once

// 展示用的数值格式化（纯字符串，不碰 UI）。所有函数只做文本，不涉及状态/主题。
//
// 这里合并的是**同一语义、原先各写一份**的实现；两处输出风格不同的不强行统一，各保留一个函数：
//   formatDuration     <1s 给 "0.3s"（聊天状态行用）
//   formatElapsed      <1s 给 "300ms"（Trace 表用）
//   formatTokenCount   大写 K/M，十万以上不保留小数（聊天状态行用）
//   formatCompactCount 小写 k、固定 1 位小数（Trace 表用）

#include <QString>
#include <QtGlobal>

// 字节数：1023 B / 1.5 KB / 1.0 MB（FileBrowser 与附件卡片共用）
inline QString formatByteSize(qint64 bytes)
{
	if (bytes < 1024)
		return QStringLiteral("%1 B").arg(bytes);
	if (bytes < 1024 * 1024)
		return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
	return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

// 耗时（聊天状态行）：0.3s / 12.4s / 1m30s；<=0 给空串（调用方据此隐藏）。
inline QString formatDuration(qint64 ms)
{
	if (ms <= 0)
		return QString();

	if (ms < 60000)
		return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral("s");

	const qint64 seconds = ms / 1000;
	return QStringLiteral("%1m%2s").arg(seconds / 60).arg(seconds % 60);
}

// 耗时（Trace 表）：300ms / 1.2s / 1m30s；<=0 给空串。
inline QString formatElapsed(qint64 ms)
{
	if (ms <= 0)
		return QString();
	if (ms < 1000)
		return QString::number(ms) + QStringLiteral("ms");
	if (ms < 60000)
		return QString::number(ms / 1000.0, 'f', 1) + QStringLiteral("s");
	return QString::number(ms / 60000) + QStringLiteral("m")
		+ QString::number((ms % 60000) / 1000) + QStringLiteral("s");
}

// 计数（聊天状态行）：999 / 1.2K / 12K / 1.0M / 12M —— 十万以上不保留小数。
inline QString formatTokenCount(qint64 value)
{
	if (value < 1000)
		return QString::number(value);
	if (value < 1000000)
		return QString::number(value / 1000.0, 'f', value >= 100000 ? 0 : 1) + QStringLiteral("K");
	return QString::number(value / 1000000.0, 'f', value >= 100000000 ? 0 : 1) + QStringLiteral("M");
}

// 计数（Trace 表）：999 / 1.2k / 1.0M；<=0 给空串。
inline QString formatCompactCount(qint64 value)
{
	if (value <= 0)
		return QString();
	if (value < 1000)
		return QString::number(value);
	if (value < 1000000)
		return QString::number(value / 1000.0, 'f', 1) + QStringLiteral("k");
	return QString::number(value / 1000000.0, 'f', 1) + QStringLiteral("M");
}
