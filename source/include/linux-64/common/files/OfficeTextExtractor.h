#pragma once

// 办公文件（OOXML）文本提取：docx / xlsx / pptx。
// 用 ArchiveExtractor 解包（Qt QZipReader，不依赖外部 tar），再用 QXmlStreamReader 读 XML。
#include <QString>

namespace OfficeTextExtractor
{
// 支持 .docx/.xlsx/.pptx；成功返回 true 并写 text，失败写 error。
bool extract(const QString& path, QString* text, QString* error);
bool isSupported(const QString& path);
} // namespace OfficeTextExtractor
