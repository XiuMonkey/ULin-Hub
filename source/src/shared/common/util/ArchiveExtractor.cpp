#include "common/util/ArchiveExtractor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QtCore/private/qzipreader_p.h>

#include <QDebug>

namespace ArchiveExtractor
{

bool isSupported(const QString& archivePath)
{
	const QString suffix = QFileInfo(archivePath).suffix().toLower();
	return suffix == QLatin1String("zip") || suffix == QLatin1String("ext");
}

bool extractTo(const QString& archivePath, const QString& destDir, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error)
			*error = message;
		qWarning().noquote() << "[Archive]" << message;
		return false;
	};

	QElapsedTimer timer;
	timer.start();

	QDir().mkpath(destDir);

	QZipReader reader(archivePath);
	if (!reader.isReadable())
		return fail(QStringLiteral("cannot open archive: %1").arg(archivePath));

	const QList<QZipReader::FileInfo> infos = reader.fileInfoList();

	// 有些打包工具既丢目录标志也丢结尾斜杠；凡是别的条目的父路径，都按目录对待。
	// 另：反斜杠路径（Compress-Archive 会这么写）统一换成 `/` 再判断。
	QSet<QString> directoryPaths;
	for (const QZipReader::FileInfo& info : infos) {
		QString normalized = info.filePath;
		normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
		const bool markedDirectory = info.isDir || normalized.endsWith(QLatin1Char('/'));
		normalized = QDir::cleanPath(normalized);
		if (markedDirectory)
			directoryPaths.insert(normalized);

		qsizetype separator = normalized.indexOf(QLatin1Char('/'));
		while (separator >= 0) {
			directoryPaths.insert(normalized.left(separator));
			separator = normalized.indexOf(QLatin1Char('/'), separator + 1);
		}
	}

	for (const QZipReader::FileInfo& info : infos) {
		QString entryPath = info.filePath;
		entryPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
		entryPath = QDir::cleanPath(entryPath);

		// 路径安全：绝对路径 / 穿越 / 盘符，一律拒整包（不静默跳过，避免"解了一半"的怪状态）
		if (entryPath.isEmpty() || entryPath == QStringLiteral(".")
			|| entryPath == QStringLiteral("..") || entryPath.startsWith(QStringLiteral("../"))
			|| QDir::isAbsolutePath(entryPath) || entryPath.contains(QLatin1Char(':'))) {
			return fail(QStringLiteral("unsafe path in archive: %1").arg(info.filePath));
		}

		const QString destPath = QDir(destDir).filePath(entryPath);
		const bool isDirectory = info.isDir || info.filePath.endsWith(QLatin1Char('/'))
			|| info.filePath.endsWith(QLatin1Char('\\')) || directoryPaths.contains(entryPath);
		if (isDirectory) {
			if (!QDir().mkpath(destPath) && !QFileInfo(destPath).isDir())
				return fail(QStringLiteral("cannot create directory: %1").arg(destPath));
			continue;
		}

		// QZipReader 对"没有结尾斜杠、也没有目录标志"的目录条目会当成文件报出来；
		// 这类条目在前面的文件落盘时已经建好，跳过即可。
		if (QFileInfo(destPath).isDir())
			continue;

		const QString parentDir = QFileInfo(destPath).absolutePath();
		if (!QDir().mkpath(parentDir))
			return fail(QStringLiteral("cannot create parent directory: %1").arg(parentDir));

		QFile outFile(destPath);
		if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
			return fail(QStringLiteral("cannot write file: %1 (%2)").arg(destPath, outFile.errorString()));

		QByteArray data = reader.fileData(info.filePath);
		if (data.isEmpty() && entryPath != info.filePath)
			data = reader.fileData(entryPath);

		if (data.isEmpty() && info.size > 0) {
			outFile.close();
			return fail(QStringLiteral("extracted file is empty: %1").arg(info.filePath));
		}

		outFile.write(data);
		outFile.close();
	}

	qInfo().noquote() << "[Archive] extracted" << archivePath << "->" << destDir
		<< QStringLiteral("elapsed=%1ms").arg(timer.elapsed());
	return true;
}

} // namespace ArchiveExtractor
