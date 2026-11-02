#pragma once

// 文件浏览领域服务：只负责“列目录 / 分页读文本文件”，不碰面板坞与工作区路由。
// 大文件按行分页：每次只把一页交给高亮/渲染，避免一次性把几十 MB 塞进 QML。

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVector>

class QProcess;

class FileBrowserService : public QObject
{
	Q_OBJECT

public:
	explicit FileBrowserService(QObject* parent = nullptr);

	QString rootPath() const { return m_rootPath; }
	QString currentPath() const { return m_currentPath; }
	QVariantList entries() const { return m_entries; }
	QString text() const { return m_text; }
	QString fileName() const { return m_fileName; }
	bool textMode() const { return m_textMode; }
	QString error() const { return m_error; }
	QString officeKind() const { return m_officeKind; }
	QString officeText() const { return m_officeText; }
	QString officePdfBase64() const { return m_officePdfBase64; }
	QString officeError() const { return m_officeError; }
	bool officeConverting() const { return m_officeConverting; }
	int pageIndex() const { return m_pageIndex; }
	int pageCount() const { return m_pageCount; }
	int totalLines() const { return m_lineOffsets.isEmpty() ? 0 : m_lineOffsets.size() - 1; }
	int pageStartLine() const { return m_pageStartLine; }
	bool canNextPage() const { return m_pageIndex + 1 < m_pageCount; }
	bool canPrevPage() const { return m_pageIndex > 0; }

	// 扩展提供的 files 后端：有 convertOffice(source, target) + officeConversionFinished(...)
	// 信号时，office 转换走扩展（LibreOffice Headless），不再走内置 Office/WPS COM。
	void setOfficeBackend(QObject* backend);

	void openRoot(const QString& path);
	void navigate(const QString& path);
	void openFile(const QString& path);
	void goUp();
	void refresh();
	void closeFile();
	void nextPage();
	void prevPage();
	// 连续滚动用：按页取文本，不改变当前页状态（QML 自己维护已加载块）。
	QString readPage(int index) const;
	int pageStartLineAt(int index) const;
	int pageLineCountAt(int index) const;
	int pageSpan() const { return kPageLines; }

signals:
	void rootPathChanged();
	void currentPathChanged();
	void entriesChanged();
	void fileChanged();
	void errorChanged();

private:
	bool isInsideRoot(const QString& path) const;
	bool buildLineIndex(const QString& path);
	bool buildLineIndexFromBuffer(const QByteArray& data);
	void loadBufferPage(int index);
	void loadPage(int index);
	void setError(const QString& text);
	void clearFile();

private:
	Q_SLOT void onOfficeBackendFinished(const QString& source, const QString& pdfPath, const QString& error);

	static constexpr int kPageLines = 300;
	static constexpr qint64 kMaxFileBytes = 64LL * 1024 * 1024;

	QString m_rootPath;
	QString m_currentPath;
	QVariantList m_entries;
	QString m_text;
	QString m_fileName;
	QString m_error;
	QString m_officeKind;   // docx/xlsx/pptx；空 = 普通文本
	QString m_officeText;
	QString m_officePdfBase64;   // Office/WPS 导出的 PDF；空 = 转换不可用
	QString m_officeError;
	QString m_tempPdfPath;
	QProcess* m_convertProc = nullptr;
	QPointer<QObject> m_officeBackend;
	bool m_officeConverting = false;   // 办公文件完整提取文本，供 QML 做文档/表格/幻灯片排版
	bool m_textMode = false;

	QString m_filePath;
	QByteArray m_buffer;      // 办公文件提取后的文本；非空时按内存文本分页
	bool m_fromBuffer = false;
	QVector<qint64> m_lineOffsets;   // 每一行的起始字节偏移；末尾再加一个文件大小哨兵
	int m_pageIndex = 0;
	int m_pageCount = 0;
	int m_pageStartLine = 0;
};
