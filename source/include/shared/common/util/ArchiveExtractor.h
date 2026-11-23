#pragma once

// 统一解包器：.ext / .zip / OOXML（docx/xlsx/pptx 本质都是 zip）走同一条实现。只是工具，不含任何安装策略
// —— 解到哪、装什么、要不要覆盖，仍由调用方决定。实现只用 Qt（QZipReader），不依赖 tar/7z 等外部程序，
// 三平台同一套行为；安全底线也收在这里：拒绝绝对路径、`..` 穿越、盘符与 UNC 路径。

#include <QString>

namespace ArchiveExtractor
{
// 后缀判断（zip/ext 之外默认不支持）
bool isSupported(const QString& archivePath);

// 解到 destDir（不存在会自动建）。失败时 error 带原因，已解出的文件不回滚。
bool extractTo(const QString& archivePath, const QString& destDir, QString* error);
}
