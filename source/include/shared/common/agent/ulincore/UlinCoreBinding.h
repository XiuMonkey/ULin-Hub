#pragma once

#include "common/agent/ulincore/UlinCoreIntent.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace UlinCore
{
// 参数引用解析：{"$param":"name"} → params["name"]。
// 默认只替换参数引用；其他值原样递归。
QJsonValue resolveBinding(const QJsonValue& value, const QJsonObject& params);
QJsonObject resolveBindings(const QJsonObject& args, const QJsonObject& params);

// Prompt 模板展开：支持 [name] 和 {{name}}。
QString expandPromptTemplate(const QString& text, const QJsonObject& params);

// 从参数声明生成 JSON Schema 对象。
QJsonObject parametersSchema(const QVector<IntentParameter>& parameters);

// 从本次 args 推断参数声明（默认值 = 本次值）。
QVector<IntentParameter> inferParameters(const QJsonObject& args);

QString jsonTypeName(const QJsonValue& value);
} // namespace UlinCore
