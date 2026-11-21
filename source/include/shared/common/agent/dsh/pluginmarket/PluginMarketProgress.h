#pragma once

// /dsh-market/status → 进度条要显示的东西。纯函数、不碰 QObject / 轮询 / UI 文案状态，
// 方便脱离控制器单测（进度解析写错是最容易"看起来有进度条但数字是假的"的一类问题）。
//
// 数据来源：dshmarket 用 pnpm `--reporter=ndjson` 维护的 progress 快照
// （phase / done / total / currentPackage / downloaded / size / lastLine / cancelling）。
// 能算出真实百分比的情况（优先级从高到低）：
//   ① phase + size/downloaded：pnpm 报的是"当前这个包/文件已下载字节 ÷ 总字节"，实时、可信；
//   ② done/total：市场自己数的包数（pnpm 11 的 tracker 目前不写 total，所以很少命中）；
//   ③ lastLine 里的 "resolved N, reused X, downloaded Y, added Z" 人读行。
// 都没有 ⇒ percent = -1，界面走不定态 + 阶段/包名/已处理数，**不编造数字**。
// 百分比压到 4%..96%：最后几步（校验/收尾）完成前不显示 100%。

#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace MarketProgress
{
	struct View
	{
		bool active = false;
		QString phase;           // resolving / downloading / linking / building（原始值）
		double percent = -1.0;   // < 0 = 没有可信百分比
		QString text;            // 一行文案：阶段 · 包名 · MB · 包数 · 百分比 · 耗时
		int seconds = 0;
		bool cancelling = false;
		int stage = 0;            // 0=未知 1=解析依赖 2=下载 3=链接 4=构建（真实阶段，来自 phase）
		qint64 downloaded = -1;  // 字节；-1 = 未知
		qint64 size = -1;
		int done = 0;
		int total = 0;
	};

	inline double percentFromPnpmLine(const QString& line)
	{
		static const QRegularExpression re(QStringLiteral(
			"resolved (\\d+), reused (\\d+), downloaded (\\d+), added (\\d+)"));
		const QRegularExpressionMatch match = re.match(line);
		if (!match.hasMatch())
			return -1.0;

		const int total = match.captured(1).toInt();
		if (total <= 0)
			return -1.0;
		const int done = match.captured(2).toInt() + match.captured(3).toInt()
			+ match.captured(4).toInt();
		return qBound(4.0, static_cast<double>(done) / total * 100.0, 96.0);
	}

	inline QString phaseLabel(const QString& phase)
	{
		if (phase == QLatin1String("resolving"))
			return QStringLiteral("解析依赖");
		if (phase == QLatin1String("downloading"))
			return QStringLiteral("下载中");
		if (phase == QLatin1String("linking"))
			return QStringLiteral("链接依赖");
		return QStringLiteral("构建中");
	}

	// fallbackText：状态里什么阶段信息都没有时的兜底文案（调用方的 installStatus）。
	inline View interpretStatus(const QJsonObject& status, const QString& fallbackText = QString())
	{
		View view;
		if (status.isEmpty())
			return view;

		const bool busy = status.value(QStringLiteral("busy")).toBool();
		const bool active = status.value(QStringLiteral("active")).toBool();
		if (!busy && !active)
			return view;

		view.active = true;
		view.seconds = status.value(QStringLiteral("seconds")).toInt();
		view.cancelling = status.value(QStringLiteral("cancelling")).toBool();
		view.phase = status.value(QStringLiteral("phase")).toString();

		const QString lastLine = status.value(QStringLiteral("lastLine")).toString().trimmed();
		const QString currentPackage = status.value(QStringLiteral("currentPackage")).toString();
		view.done = status.value(QStringLiteral("done")).toInt();
		view.total = status.value(QStringLiteral("total")).toInt();
		const double downloaded = status.value(QStringLiteral("downloaded")).toDouble(-1.0);
		const double size = status.value(QStringLiteral("size")).toDouble(-1.0);
		if (downloaded >= 0.0)
			view.downloaded = static_cast<qint64>(downloaded);
		if (size > 0.0)
			view.size = static_cast<qint64>(size);

		if (view.phase == QLatin1String("resolving"))
			view.stage = 1;
		else if (view.phase == QLatin1String("downloading"))
			view.stage = 2;
		else if (view.phase == QLatin1String("linking"))
			view.stage = 3;
		else if (view.phase == QLatin1String("building"))
			view.stage = 4;

		// 只有"市场数出来的包数 / pnpm 人读行的 resolved 总数"才是整体口径。
		// 字节进度是当前这个包的，拿它当整体百分比会乱跳（真机反馈"不是真实进度"就是它），所以不算 percent。
		if (view.total > 0 && view.done > 0)
			view.percent = qBound(4.0, static_cast<double>(view.done) / view.total * 100.0, 96.0);
		// ③ pnpm 的人读进度行
		else
			view.percent = percentFromPnpmLine(lastLine);

		QStringList parts;
		if (!view.phase.isEmpty())
			parts << phaseLabel(view.phase);
		else if (!lastLine.isEmpty())
			parts << lastLine;
		if (!currentPackage.isEmpty())
			parts << currentPackage;
		if (view.size > 0 && view.downloaded >= 0) {
			// MB 保留 1 位；小文件（<0.1MB）给 KB，避免一直显示 0.0 MB
			if (view.size >= 1024 * 1024) {
				parts << QStringLiteral("%1 / %2 MB")
					.arg(QString::number(view.downloaded / 1048576.0, 'f', 1),
						QString::number(view.size / 1048576.0, 'f', 1));
			}
			else {
				parts << QStringLiteral("%1 / %2 KB")
					.arg(QString::number(view.downloaded / 1024.0, 'f', 0),
						QString::number(view.size / 1024.0, 'f', 0));
			}
		}
		if (view.done > 0) {
			parts << (view.total > 0
				? QStringLiteral("已完成 %1 / %2 个包").arg(view.done).arg(view.total)
				: QStringLiteral("已完成 %1 个包").arg(view.done));
		}
		if (view.percent >= 0.0)
			parts << QStringLiteral("%1%").arg(qRound(view.percent));
		if (view.seconds > 0)
			parts << QStringLiteral("%1s").arg(view.seconds);

		if (parts.isEmpty() && !lastLine.isEmpty())
			parts << lastLine;
		if (parts.isEmpty())
			parts << (fallbackText.isEmpty() ? QStringLiteral("正在处理…") : fallbackText);
		view.text = parts.join(QStringLiteral(" · "));

		if (view.cancelling)
			view.text.prepend(QStringLiteral("正在取消 · "));

		return view;
	}
}   // namespace MarketProgress
