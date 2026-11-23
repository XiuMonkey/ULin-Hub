#pragma once

// 纯 token 数据与 metrics.json 覆盖规则：放 common，ThemeBridge 只做 QML 属性面。
#include <QVariantMap>

class QJsonObject;

namespace ThemeTokens
{
// 各组内置默认 token（metrics.json 的缺项按它兜底）
QVariantMap radius();
QVariantMap font();
QVariantMap spacing();
QVariantMap size();
QVariantMap shadow();
QVariantMap motion();
// 把 metrics.json 里 group 分组的键覆盖进 target，未出现的键保留原值
void applyMetrics(QVariantMap& target, const QJsonObject& root, const QString& group);
}
