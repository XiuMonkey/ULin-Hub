#include "qmlbridge/ExtensionsBridge.h"

ExtensionsBridge::ExtensionsBridge(ExtensionManagerPage* page, QObject* parent)
	: QObject(parent)
	, m_page(page)
{
	connect(m_page, &ExtensionManagerPage::openChanged, this, &ExtensionsBridge::openChanged);
	connect(m_page, &ExtensionManagerPage::itemsChanged, this, &ExtensionsBridge::itemsChanged);
	connect(m_page, &ExtensionManagerPage::statusChanged, this, &ExtensionsBridge::statusChanged);
	connect(m_page, &ExtensionManagerPage::busyChanged, this, &ExtensionsBridge::busyChanged);
	connect(m_page, &ExtensionManagerPage::selectedIndexChanged, this, &ExtensionsBridge::selectedIndexChanged);
	connect(m_page, &ExtensionManagerPage::restartHintChanged, this, &ExtensionsBridge::restartHintChanged);
}
