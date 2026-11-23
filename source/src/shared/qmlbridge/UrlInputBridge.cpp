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
