// 见 UrlInputBridge.h：QML 单例 `UrlInput`，只把调用转给 normalizeUrlInput()。

#include "qmlbridge/UrlInputBridge.h"

#include "common/util/UrlInput.h"

UrlInputBridge& UrlInputBridge::instance()
{
	static UrlInputBridge bridge;
	return bridge;
}

UrlInputBridge::UrlInputBridge(QObject* parent)
	: QObject(parent)
{
}

QString UrlInputBridge::normalize(const QString& text) const
{
	return normalizeUrlInput(text);
}
