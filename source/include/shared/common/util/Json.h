#pragma once

// 轻量 JSON 辅助：不解析、不校验，只做形状转换。
// - idListFromJsonArray：QJsonArray 里取非空字符串 id（会话置顶列表等）
// - jsonIndented：QJsonObject -> 缩进后的文本（工具参数/事件详情的展示）

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

inline QStringList idListFromJsonArray(const QJsonArray& values)
{
	QStringList ids;
	ids.reserve(values.size());
	for (const QJsonValue& value : values) {
		const QString id = value.toString();
		if (!id.isEmpty())
			ids.append(id);
	}
	return ids;
}

inline QString jsonIndented(const QJsonObject& object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}
