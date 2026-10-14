#pragma once

// QML 面：地址栏输入的规范化（QML 单例名 `UrlInput`，注册见 main.cpp）。
// 真正的规则在 common/util/UrlInput.h 的 normalizeUrlInput() —— 这里**只做转调**。
//
// 为什么单独一个单例、而不是挂到 Hub（QmlBridge）上：QmlBridge 的契约是"接线 + 纯界面状态"，
// 而这是一条**无状态纯函数**；单列一个单例后 QML 侧读起来是 `UrlInput.normalize(text)`，
// 归属清楚，也不必为了一个函数去动 QML 唯一那扇门。注册机制与 WindowBridge / ThemeBridge
// 完全一样（qmlRegisterSingletonInstance，"C++ 名字带 Bridge、QML 名字取短名"也是同一套惯例）。

#include <QObject>
#include <QString>

class UrlInputBridge : public QObject
{
	Q_OBJECT

public:
	static UrlInputBridge& instance();   // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎

	// 见 normalizeUrlInput()：返回空串表示"这行不该导航"（调用方直接 return）
	Q_INVOKABLE QString normalize(const QString& text) const;

private:
	explicit UrlInputBridge(QObject* parent = nullptr);
	UrlInputBridge(const UrlInputBridge&) = delete;
	UrlInputBridge& operator=(const UrlInputBridge&) = delete;
};
