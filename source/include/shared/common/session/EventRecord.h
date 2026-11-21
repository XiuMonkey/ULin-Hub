#pragma once

// 会话记录（session 事件流）的外壳拆解。
// 服务端推送/持久化的一行可能是 { seq, event: {...} } 也可能直接是事件对象，
// 需要原始事件的地方统一走这里，别各自写一遍。
// 原先 ChatController.cpp 与 TraceModel.cpp 各有一份**逐字相同**的实现。

#include <QJsonObject>
#include <QJsonValue>

inline QJsonObject unwrapRecord(const QJsonValue& value)
{
	QJsonObject event = value.toObject();
	if (event.value(QStringLiteral("event")).isObject())
		event = event.value(QStringLiteral("event")).toObject();
	return event;
}
