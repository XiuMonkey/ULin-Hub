#pragma once

// ChatController 拆目录后的内部共享工具：常量、探针开关、事件分类/记录解析。
// 只给 src/shared/common/session/chat/*.cpp 用，不属于公开头。

#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>
#include <QtMath>

namespace chatctl
{
	inline constexpr int kStreamFlushMs = 33;
	// 会话行数：运行期只在**用户发消息**时判断一次；超过 60 行就收到 60。
	// 被收掉的历史仍在 m_historyRecords 里，点「加载更多」会重新拼回来。
	inline constexpr int kRowTrigger = 60;
	inline constexpr int kRowKeep = 60;

	// 高频日志开关：重建/流式时每条 chunk 一行会把主线程写日志写死，默认关；排查时设 ULIN_CHAT_TRACE=1。
	inline bool chatTraceEnabled()
	{
		static const bool enabled = qEnvironmentVariableIsSet("ULIN_CHAT_TRACE");
		return enabled;
	}

	inline QString textDigest(const QString& text)
	{
		return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),
			QCryptographicHash::Sha1).toHex().left(12));
	}

	inline constexpr int kTurnWatchdogMs = 30000;        // 这么久没有本会话事件就认为后端没在干活（没配模型 / resume 失败）
	inline constexpr int kCancelRepeatGuardMs = 1500;    // 停止键连点去重窗口：只认第一次，期间靠 cancelling 表态
	inline constexpr int kCancelConfirmMs = 8000;        // 发出 cancel 后等不到 turn/end 就本地强制收尾（服务端可能没实现中断）

	// 隐藏的上下文注入（运行时快照/系统说明/目录）不是用户真正发的消息。
	// 另外两类同样要藏 —— 子代理回传给父会话的话（kind=agent-message）与"后台子代理结束"通知（kind=subagent-settled）：
	// 两者都是以 role=user 记进账本的 inbox 注入，画成用户气泡会让人以为是自己在说话。
	inline QJsonObject userMessageSource(const QJsonObject& event)
	{
		const QJsonObject data = event.value(QStringLiteral("data")).toObject();
		QJsonObject message = data.value(QStringLiteral("message")).toObject();
		if (message.isEmpty())
			message = data;
		return message.value(QStringLiteral("source")).toObject();
	}

	inline bool isPeerRelayUserMessage(const QJsonObject& event)
	{
		const QString kind = userMessageSource(event).value(QStringLiteral("kind")).toString();
		return kind == QStringLiteral("agent-message")
			|| kind == QStringLiteral("subagent-settled");
	}

	// 切 provider/model 时引擎注入的 user 角色通知：`[model changed: ...]`
	// （source.kind = model-selection、form = notice）。它是给模型看的上下文说明，
	// 不是用户说的话 —— 画成用户气泡会让人以为是自己发的，应按系统提示行渲染。
	inline bool isModelSelectionNotice(const QJsonObject& event)
	{
		return userMessageSource(event).value(QStringLiteral("kind")).toString()
			== QStringLiteral("model-selection");
	}

	inline bool isHiddenContextUserMessage(const QJsonObject& event)
	{
		const QJsonObject source = userMessageSource(event);
		const QString kind = source.value(QStringLiteral("kind")).toString();
		// 运行上下文 / 时间戳 / 压缩检查点都是引擎注入给模型的 user 角色消息，
		// 不是用户输入；历史回放时必须隐藏，否则整段摘要会显示成用户气泡。
		if (kind == QStringLiteral("runtime-context")
			|| kind == QStringLiteral("time-context")
			|| kind == QStringLiteral("compact-checkpoint"))
			return true;
		const QString form = source.value(QStringLiteral("form")).toString();
		if (form == QStringLiteral("snapshot")
			|| form == QStringLiteral("instructions")
			|| form == QStringLiteral("catalog"))
			return true;
		return isPeerRelayUserMessage(event);
	}

	inline int recordSeq(const QJsonValue& value)
	{
		const QJsonObject record = value.toObject();
		int seq = record.value(QStringLiteral("seq")).toInt();
		if (seq <= 0 && record.value(QStringLiteral("event")).isObject())
			seq = record.value(QStringLiteral("event")).toObject().value(QStringLiteral("seq")).toInt();
		return seq;
	}

	// 表示"回合真的在推进"的事件类型（看门狗只认这些续期）；刻意不含会话生命周期/投影/标题/命令变更 —— 它们会周期性推来。
	inline bool isTurnProgressEvent(const QString& type)
	{
		return type.startsWith(QLatin1String("assistant/"))
			|| type == QLatin1String("text-chunks")
			|| type == QLatin1String("reasoning-chunks")
			|| type == QLatin1String("tool/call")
			|| type == QLatin1String("tool/result")
			|| type == QLatin1String("step/start")
			|| type == QLatin1String("step/end")
			|| type == QLatin1String("turn/start");
	}

	// ---- 统计行剩余的两个格式化函数（duration/tokens 的实现在 util/Format.h） ----

	inline QString formatThroughput(double value)
	{
		return QString::number(value, 'f', value >= 10.0 ? 0 : 1);
	}

	inline QString formatCacheHitPercent(qint64 billedInput, qint64 cacheRead)
	{
		if (billedInput <= 0)
			return QString();
		if (cacheRead >= billedInput)
			return QStringLiteral("100");

		const double ratio = static_cast<double>(cacheRead) / static_cast<double>(billedInput) * 100.0;
		const int rounded = qRound(ratio);
		if (rounded < 100)
			return QString::number(rounded);

		// 四舍五入到 100 但并未真的 100：依次给 1–4 位小数，兜底 99.99
		for (int digits = 1; digits <= 4; ++digits) {
			const QString text = QString::number(ratio, 'f', digits);
			if (text != QLatin1String("100") && text != QLatin1String("100.0")
				&& text != QLatin1String("100.00") && text != QLatin1String("100.000")
				&& text != QLatin1String("100.0000")) {
				return text;
			}
		}
		return QStringLiteral("99.99");
	}
}

