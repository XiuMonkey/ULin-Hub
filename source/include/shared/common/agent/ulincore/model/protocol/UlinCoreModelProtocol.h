#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace UlinCore::Protocol
{
// ULin 自有模型边界的保留参数。它只存在于“发给模型/从模型返回”的线上格式，
// Kernel 内部仍然使用 IntentCall.metadata，不把协议字段泄漏到 Intent/Executor。
inline constexpr char kMetadataKey[] = "__ulincore_meta";

// 给每个 function tool 的 parameters 注入可选的 __ulincore_meta。
QJsonArray encodeToolSchemas(const QJsonArray& tools);

// 把 IntentCall.metadata 合入 arguments，供模型请求/历史消息使用。
QJsonObject encodeToolArguments(const QJsonObject& arguments, const QJsonObject& metadata);

// 从模型返回的 arguments 中取出 metadata，并从参数里删除保留键。
QJsonObject decodeToolArguments(const QJsonObject& arguments, QJsonObject* metadata);

QJsonObject metadataSchema();
} // namespace UlinCore::Protocol
