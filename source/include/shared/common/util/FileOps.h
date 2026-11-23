#pragma once

// 目录级文件操作：原先各平台 ExtensionLoader.cpp 里逐字相同的实现收拢到一处，
// 避免拷贝清单、BOM 处理这类细节改一处漏一处。
//
// 这些是"尽力而为"的目录操作，失败时通过 error 回传原因；调用方拿 false 就别继续装。

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

// 递归复制 srcPath 到 dstPath。目标文件已存在时先删再拷（QFile::copy 不覆盖），
// 目标目录不存在会创建。
inline bool copyRecursively(const QString& srcPath, const QString& dstPath, QString* error)
{
	const QDir srcDir(srcPath);
	if (!srcDir.exists()) {
		if (error)
			*error = QStringLiteral("source directory does not exist: %1").arg(srcPath);
		return false;
	}

	QDir dstDir(dstPath);
	if (!dstDir.exists() && !dstDir.mkpath(QStringLiteral("."))) {
		if (error)
			*error = QStringLiteral("cannot create directory: %1").arg(dstPath);
		return false;
	}

	const QFileInfoList entries = srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QFileInfo& info : entries) {
		const QString target = dstPath + QStringLiteral("/") + info.fileName();
		if (info.isDir()) {
			if (!copyRecursively(info.absoluteFilePath(), target, error))
				return false;
		}
		else {
			if (QFile::exists(target) && !QFile::remove(target)) {
				if (error)
					*error = QStringLiteral("cannot remove old file: %1").arg(target);
				return false;
			}
			if (!QFile::copy(info.absoluteFilePath(), target)) {
				if (error)
					*error = QStringLiteral("cannot copy file: %1 -> %2").arg(info.absoluteFilePath(), target);
				return false;
			}
		}
	}

	return true;
}

// 删除整棵目录；目录本来就不存在时 QDir::removeRecursively() 也返回 true，调用方不用先判断存在。
inline bool removeRecursively(const QString& path)
{
	QDir dir(path);
	return dir.removeRecursively();
}

// 递归去掉目录下所有 *.json 的 UTF-8 BOM：Node 的 JSON.parse 遇到 BOM 会直接报错，
// 而 qrc 里的文件不保证没 BOM（编辑器/转换工具带进来的）。
inline void stripUtf8BomFromJsonFiles(const QString& dirPath)
{
	QDirIterator it(dirPath, { QStringLiteral("*.json") }, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QString filePath = it.next();
		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly))
			continue;

		QByteArray data = file.readAll();
		file.close();

		if (data.startsWith("\xEF\xBB\xBF")) {
			QByteArray cleaned = data.mid(3);
			if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
				file.write(cleaned);
				file.close();
			}
		}
	}
}
