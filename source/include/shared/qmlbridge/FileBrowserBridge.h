#pragma once

// QML 接线层：只把 FileBrowserService 暴露成 QML 单例 FileBrowser。
// 目录、文本读取、办公文档解析、分页等逻辑都在 common/files/ 下，不在本文件。

#include "common/files/FileBrowserService.h"

#include <QObject>
#include <QString>
#include <QVariantList>

class FileBrowserBridge : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
	Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
	Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
	Q_PROPERTY(QString text READ text NOTIFY fileChanged)
	Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
	Q_PROPERTY(bool textMode READ textMode NOTIFY fileChanged)
	Q_PROPERTY(QString error READ error NOTIFY errorChanged)
	Q_PROPERTY(QString officeKind READ officeKind NOTIFY fileChanged)
	Q_PROPERTY(QString officeText READ officeText NOTIFY fileChanged)
	Q_PROPERTY(QString officePdfBase64 READ officePdfBase64 NOTIFY fileChanged)
	Q_PROPERTY(QString officeError READ officeError NOTIFY fileChanged)
	Q_PROPERTY(bool officeConverting READ officeConverting NOTIFY fileChanged)
	Q_PROPERTY(QString officeViewerUrl READ officeViewerUrl CONSTANT)
	Q_PROPERTY(QString officeViewerHtml READ officeViewerHtml CONSTANT)
	Q_PROPERTY(int pageIndex READ pageIndex NOTIFY fileChanged)
	Q_PROPERTY(int pageCount READ pageCount NOTIFY fileChanged)
	Q_PROPERTY(int totalLines READ totalLines NOTIFY fileChanged)
	Q_PROPERTY(int pageStartLine READ pageStartLine NOTIFY fileChanged)
	Q_PROPERTY(bool canNextPage READ canNextPage NOTIFY fileChanged)
	Q_PROPERTY(bool canPrevPage READ canPrevPage NOTIFY fileChanged)

public:
	static FileBrowserBridge& instance();
	explicit FileBrowserBridge(QObject* parent = nullptr);

	QString rootPath() const { return m_service.rootPath(); }
	QString currentPath() const { return m_service.currentPath(); }
	QVariantList entries() const { return m_service.entries(); }
	QString text() const { return m_service.text(); }
	QString fileName() const { return m_service.fileName(); }
	bool textMode() const { return m_service.textMode(); }
	QString error() const { return m_service.error(); }
	QString officeKind() const { return m_service.officeKind(); }
	QString officeText() const { return m_service.officeText(); }
	QString officePdfBase64() const { return m_service.officePdfBase64(); }
	QString officeError() const { return m_service.officeError(); }
	bool officeConverting() const { return m_service.officeConverting(); }
	QString officeViewerUrl() const;
	void ensureOfficeViewerFiles() const;
	QString officeViewerHtml() const;
	int pageIndex() const { return m_service.pageIndex(); }
	int pageCount() const { return m_service.pageCount(); }
	int totalLines() const { return m_service.totalLines(); }
	int pageStartLine() const { return m_service.pageStartLine(); }
	bool canNextPage() const { return m_service.canNextPage(); }
	bool canPrevPage() const { return m_service.canPrevPage(); }

	// files kind 的扩展后端（LibreOffice Headless 等）由 QmlBridge 在扩展登记后接入。
	void setOfficeBackend(QObject* backend) { m_service.setOfficeBackend(backend); }

	Q_INVOKABLE void openRoot(const QString& path) { m_service.openRoot(path); }
	Q_INVOKABLE void navigate(const QString& path) { m_service.navigate(path); }
	Q_INVOKABLE void openFile(const QString& path) { m_service.openFile(path); }
	Q_INVOKABLE void goUp() { m_service.goUp(); }
	Q_INVOKABLE void refresh() { m_service.refresh(); }
	Q_INVOKABLE void closeFile() { m_service.closeFile(); }
	Q_INVOKABLE void nextPage() { m_service.nextPage(); }
	Q_INVOKABLE void prevPage() { m_service.prevPage(); }
	Q_INVOKABLE QString readPage(int index) const { return m_service.readPage(index); }
	Q_INVOKABLE int pageStartLineAt(int index) const { return m_service.pageStartLineAt(index); }
	Q_INVOKABLE int pageLineCountAt(int index) const { return m_service.pageLineCountAt(index); }
	Q_INVOKABLE int pageSpan() const { return m_service.pageSpan(); }

signals:
	void rootPathChanged();
	void currentPathChanged();
	void entriesChanged();
	void fileChanged();
	void errorChanged();

private:
	FileBrowserService m_service;
};
