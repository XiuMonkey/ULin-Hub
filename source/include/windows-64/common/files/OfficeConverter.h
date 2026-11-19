#pragma once

// Office 文档转 PDF：调用本机 Microsoft Office / WPS 的 COM 自动化。
// 找不到 Office/WPS 时返回 false，由上层显示“无法解析，展示原始文本”。
#include <QString>

class QProcess;

namespace OfficeConverter
{
bool convertToPdf(const QString& sourcePath, const QString& pdfPath, QString* error);
QProcess* startConversion(const QString& sourcePath, const QString& pdfPath, QObject* parent);
QString friendlyError(const QString& raw);
bool isOfficeDocument(const QString& path);
} // namespace OfficeConverter
