#pragma once

// Office 文档转 PDF：调本机 LibreOffice（soffice）的无头转换。
// 找不到 soffice 或转换超时返回 false，由上层降级为展示提取出的原始文本。
#include <QString>

class QProcess;

namespace OfficeConverter
{
bool convertToPdf(const QString& sourcePath, const QString& pdfPath, QString* error);
// 异步版：返回刚 start 的进程，调用方接 finished 读结果与 stderr，并负责回收
QProcess* startConversion(const QString& sourcePath, const QString& pdfPath, QObject* parent);
QString friendlyError(const QString& raw);
// 按后缀判断，含老式 doc / xls / ppt
bool isOfficeDocument(const QString& path);
} // namespace OfficeConverter
