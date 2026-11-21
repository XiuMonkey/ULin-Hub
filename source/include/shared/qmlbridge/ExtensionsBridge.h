#pragma once

// 扩展管理页的 QML 暴露面：只组合 ExtensionManagerPage，不写页面逻辑。
#include "ExtensionSystem/ExtensionManagerPage.h"

#include <QObject>

class ExtensionsBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
	Q_PROPERTY(bool canRemove READ canRemove NOTIFY selectedIndexChanged)
	Q_PROPERTY(QString restartHint READ restartHint NOTIFY restartHintChanged)

public:
	explicit ExtensionsBridge(ExtensionManagerPage* page, QObject* parent = nullptr);

	bool isOpen() const { return m_page->isOpen(); }
	QVariantList items() const { return m_page->items(); }
	QString status() const { return m_page->status(); }
	bool busy() const { return m_page->busy(); }
	int selectedIndex() const { return m_page->selectedIndex(); }
	void setSelectedIndex(int index) { m_page->setSelectedIndex(index); }
	bool canRemove() const { return m_page->canRemove(); }
	QString restartHint() const { return m_page->restartHint(); }

	Q_INVOKABLE void openPage() { m_page->openPage(); }
	Q_INVOKABLE void closePage() { m_page->closePage(); }
	Q_INVOKABLE void refresh() { m_page->refresh(); }
	Q_INVOKABLE void installFromFile() { m_page->installFromFile(); }
	Q_INVOKABLE bool removeSelected() { return m_page->removeSelected(); }
	Q_INVOKABLE void cleanupResiduals() { m_page->cleanupResiduals(); }
	Q_INVOKABLE void restartServer() { m_page->restartServer(); }

signals:
	void openChanged();
	void itemsChanged();
	void statusChanged();
	void busyChanged();
	void selectedIndexChanged();
	void restartHintChanged();

private:
	ExtensionManagerPage* m_page = nullptr;
};
