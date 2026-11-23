#pragma once

// QML 单例 `UrlInput`（注册见 main.cpp）：只把 normalize() 转调给 common/util/UrlInput 的
// normalizeUrlInput，不承载算法 —— qmlbridge 的约定是"只做接线：属性/信号、Q_INVOKABLE 转发、
// model 角色名映射"。单列一个无状态单例，QML 侧即可写成 UrlInput.normalize(text)。

#include <QObject>
#include <QString>

class UrlInputBridge : public QObject
{
	Q_OBJECT

public:
	static UrlInputBridge& instance();   // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎

	// 见 normalizeUrlInput：返回空串表示"这行不该导航"（调用方直接 return）
	Q_INVOKABLE QString normalize(const QString& text) const;

private:
	explicit UrlInputBridge(QObject* parent = nullptr);
	UrlInputBridge(const UrlInputBridge&) = delete;
	UrlInputBridge& operator=(const UrlInputBridge&) = delete;
};
