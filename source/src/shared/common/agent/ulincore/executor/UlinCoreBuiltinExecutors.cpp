#include "common/agent/ulincore/executor/UlinCoreBuiltinExecutors.h"

#include "common/agent/ulincore/executor/UlinCoreNativeExecutor.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <memory>

namespace UlinCore
{
namespace
{
	constexpr qint64 kMaxReadBytes = 1024 * 1024;
	constexpr qint64 kMaxSearchFileBytes = 1024 * 1024;

	// 工作区根目录取 canonical 路径，失败时退化为 clean 后的绝对路径。
	QString cleanRoot(const QString& root)
	{
		if (root.trimmed().isEmpty())
			return QDir::currentPath();
		const QString canonical = QFileInfo(root).canonicalFilePath();
		return canonical.isEmpty() ? QDir::cleanPath(QDir(root).absolutePath()) : canonical;
	}

	// 把路径钉死在 workspaceRoot 里：越界路径一律拒绝（绝对路径落在根内时仍接受）。
	QString resolveInside(const QString& root, const QString& raw, QString* error)
	{
		const QString value = raw.trimmed().isEmpty() ? QStringLiteral(".") : raw.trimmed();
		const QDir rootDir(root);
		const QString candidate = QDir::isAbsolutePath(value)
			? QDir::cleanPath(value)
			: rootDir.absoluteFilePath(value);

		const QFileInfo info(candidate);
		QString canonical = info.canonicalFilePath();
		if (canonical.isEmpty()) {
			// 文件还不存在（write_file）：用父目录的 canonical 路径拼回来。
			const QDir parent(info.absolutePath());
			QString parentCanonical = parent.canonicalPath();
			if (parentCanonical.isEmpty())
				parentCanonical = QDir::cleanPath(parent.absolutePath());
			canonical = QDir(parentCanonical).absoluteFilePath(info.fileName());
		}

		const QString normalizedRoot = cleanRoot(root);
		const Qt::CaseSensitivity cs = Qt::CaseInsensitive;   // Windows 路径
		const bool inside = canonical.compare(normalizedRoot, cs) == 0
			|| canonical.startsWith(normalizedRoot + QDir::separator(), cs);
		if (!inside) {
			if (error)
				*error = QStringLiteral("path escapes workspace: ") + raw;
			return QString();
		}
		return canonical;
	}

	QString relativeToRoot(const QString& root, const QString& path)
	{
		return QDir(root).relativeFilePath(path);
	}

	// 搜索时跳过版本库、依赖与构建目录，减少无关命中。
	bool isInsideSkippedDir(const QString& path)
	{
		const QString normalized = QDir::fromNativeSeparators(path);
		static const QStringList skipped{
			QStringLiteral("/.git/"),
			QStringLiteral("/node_modules/"),
			QStringLiteral("/x64/"),
			QStringLiteral("/build/"),
			QStringLiteral("/.vs/")
		};
		for (const QString& marker : skipped) {
			if (normalized.contains(marker))
				return true;
		}
		return false;
	}

	// read_file：只读文本，二进制文件直接拒绝。
	class ReadFileExecutor : public UlinCoreNativeExecutor
	{
	public:
		ExecuteResult execute(const QJsonObject& args, const ExecutorContext& ctx) override
		{
			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(), &error);
			if (path.isEmpty())
				return ExecuteResult::failure(error);

			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
				return ExecuteResult::failure(QStringLiteral("cannot open %1: %2").arg(args.value(QStringLiteral("path")).toString(), file.errorString()));

			const qint64 requested = static_cast<qint64>(args.value(QStringLiteral("maxBytes")).toInt(65536));
			const qint64 limit = qBound<qint64>(1, requested, kMaxReadBytes);
			const QByteArray raw = file.read(limit);
			if (raw.contains('\0'))
				return ExecuteResult::failure(QStringLiteral("binary file, use a dedicated viewer"));
			return ExecuteResult::success(QString::fromUtf8(raw));
		}
	};

	// list_dir：目录名带 / 后缀，条目超出上限时追加截断标记。
	class ListDirExecutor : public UlinCoreNativeExecutor
	{
	public:
		ExecuteResult execute(const QJsonObject& args, const ExecutorContext& ctx) override
		{
			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(QStringLiteral(".")), &error);
			if (path.isEmpty())
				return ExecuteResult::failure(error);

			const QDir dir(path);
			if (!dir.exists())
				return ExecuteResult::failure(QStringLiteral("not a directory: ") + args.value(QStringLiteral("path")).toString());

			const int maxEntries = qBound(1, args.value(QStringLiteral("maxEntries")).toInt(200), 2000);
			const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

			QStringList lines;
			int count = 0;
			for (const QFileInfo& info : entries) {
				if (count++ >= maxEntries) {
					lines.append(QStringLiteral("... (truncated)"));
					break;
				}
				const QString rel = dir.relativeFilePath(info.absoluteFilePath());
				lines.append(info.isDir()
					? rel + QLatin1Char('/')
					: QStringLiteral("%1 (%2 bytes)").arg(rel).arg(info.size()));
			}
			return ExecuteResult::success(lines.join(QLatin1Char('\n')));
		}
	};

	// write_file：受 allowWrites 控制，写入前自动建父目录。
	class WriteFileExecutor : public UlinCoreNativeExecutor
	{
	public:
		ExecuteResult execute(const QJsonObject& args, const ExecutorContext& ctx) override
		{
			if (!ctx.allowWrites)
				return ExecuteResult::failure(QStringLiteral("write_file is disabled (allowWrites=false)"));

			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(), &error);
			if (path.isEmpty())
				return ExecuteResult::failure(error);

			QDir().mkpath(QFileInfo(path).absolutePath());
			const QByteArray bytes = args.value(QStringLiteral("content")).toString().toUtf8();
			QFile file(path);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return ExecuteResult::failure(QStringLiteral("cannot write %1: %2").arg(args.value(QStringLiteral("path")).toString(), file.errorString()));

			const qint64 written = file.write(bytes);
			file.close();
			if (written != bytes.size())
				return ExecuteResult::failure(QStringLiteral("short write: %1/%2").arg(written).arg(bytes.size()));

			const QString relative = relativeToRoot(root, path);
			return ExecuteResult::success(QStringLiteral("wrote %1 bytes to %2").arg(written).arg(relative),
				QJsonObject{
					{ QStringLiteral("path"), relative },
					{ QStringLiteral("bytes"), written }
				});
		}
	};

	// search_text：按字面量搜索，跳过 .git/node_modules 等目录与二进制文件。
	class SearchTextExecutor : public UlinCoreNativeExecutor
	{
	public:
		ExecuteResult execute(const QJsonObject& args, const ExecutorContext& ctx) override
		{
			const QString query = args.value(QStringLiteral("query")).toString();
			if (query.isEmpty())
				return ExecuteResult::failure(QStringLiteral("query is empty"));

			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString base = resolveInside(root, args.value(QStringLiteral("path")).toString(QStringLiteral(".")), &error);
			if (base.isEmpty())
				return ExecuteResult::failure(error);

			const bool caseSensitive = args.value(QStringLiteral("caseSensitive")).toBool(false);
			const int maxResults = qBound(1, args.value(QStringLiteral("maxResults")).toInt(50), 500);
			const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

			QStringList hits;
			QDirIterator it(base, QDirIterator::Subdirectories);
			while (it.hasNext() && hits.size() < maxResults) {
				const QString path = it.next();
				const QFileInfo info = it.fileInfo();
				if (info.isDir() || isInsideSkippedDir(path) || info.size() > kMaxSearchFileBytes)
					continue;

				QFile file(path);
				if (!file.open(QIODevice::ReadOnly))
					continue;
				const QByteArray raw = file.read(kMaxSearchFileBytes);
				if (raw.contains('\0'))
					continue;

				const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
				for (int i = 0; i < lines.size() && hits.size() < maxResults; ++i) {
					if (lines.at(i).contains(query, cs)) {
						hits.append(QStringLiteral("%1:%2: %3").arg(relativeToRoot(root, path)).arg(i + 1).arg(lines.at(i).trimmed()));
					}
				}
			}

			return ExecuteResult::success(hits.isEmpty() ? QStringLiteral("no matches")
				: hits.join(QLatin1Char('\n')));
		}
	};
} // namespace

void registerBuiltinExecutors(UlinCoreNativeRegistry& registry)
{
	registry.add(QStringLiteral("builtin.read_file"), std::make_shared<ReadFileExecutor>());
	registry.add(QStringLiteral("builtin.list_dir"), std::make_shared<ListDirExecutor>());
	registry.add(QStringLiteral("builtin.write_file"), std::make_shared<WriteFileExecutor>());
	registry.add(QStringLiteral("builtin.search_text"), std::make_shared<SearchTextExecutor>());
}
} // namespace UlinCore
