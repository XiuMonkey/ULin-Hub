#pragma once

// 办公文件（OOXML）文本提取：docx / xlsx / pptx。
// 用系统 tar 解包，再用 QXmlStreamReader 读 XML；不依赖 Office 安装。
#include <QString>

namespace OfficeTextExtractor
{
// 支持 .docx/.xlsx/.pptx；成功返回 true 并写 text，失败写 error。
bool extract(const QString& path, QString* text, QString* error);
bool isSupported(const QString& path);
} // namespace OfficeTextExtractor
