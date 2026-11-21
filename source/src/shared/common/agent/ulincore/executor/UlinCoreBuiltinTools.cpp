#include "common/agent/ulincore/executor/UlinCoreBuiltinTools.h"

#include "common/agent/ulincore/executor/UlinCoreTool.h"

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

	QString cleanRoot(const QString& root)
	{
		if (root.trimmed().isEmpty())
			return QDir::currentPath();
		const QString canonical = QFileInfo(root).canonicalFilePath();
		return canonical.isEmpty() ? QDir::cleanPath(QDir(root).absolutePath()) : canonical;
	}

	// 把路径钉死在 workspaceRoot 里；越界或绝对路径一律拒绝。
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

	class ReadFileTool : public UlinCoreTool
	{
	public:
		QString name() const override { return QStringLiteral("read_file"); }
		QString description() const override
		{
			return QStringLiteral("Read a UTF-8 text file inside the workspace. Returns at most maxBytes bytes.");
		}
		QJsonObject parametersSchema() const override
		{
			return QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("object") },
				{ QStringLiteral("properties"), QJsonObject{
					{ QStringLiteral("path"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("string") },
						{ QStringLiteral("description"), QStringLiteral("Relative path from the workspace root.") }
					} },
					{ QStringLiteral("maxBytes"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("integer") },
						{ QStringLiteral("description"), QStringLiteral("Maximum bytes to read (default 65536, cap 1048576).") }
					} }
				} },
				{ QStringLiteral("required"), QJsonArray{ QStringLiteral("path") } }
			};
		}
		ToolResult execute(const QJsonObject& args, const ToolContext& ctx) override
		{
			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(), &error);
			if (path.isEmpty())
				return ToolResult::failure(error);

			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
				return ToolResult::failure(QStringLiteral("cannot open %1: %2")
					.arg(args.value(QStringLiteral("path")).toString(), file.errorString()));

			const qint64 requested = static_cast<qint64>(args.value(QStringLiteral("maxBytes")).toInt(65536));
			const qint64 limit = qBound<qint64>(1, requested, kMaxReadBytes);
			const QByteArray raw = file.read(limit);
			if (raw.contains('\0'))
				return ToolResult::failure(QStringLiteral("binary file, use a dedicated viewer"));
			return ToolResult::success(QString::fromUtf8(raw));
		}
	};

	class ListDirTool : public UlinCoreTool
	{
	public:
		QString name() const override { return QStringLiteral("list_dir"); }
		QString description() const override
		{
			return QStringLiteral("List entries of a directory inside the workspace.");
		}
		QJsonObject parametersSchema() const override
		{
			return QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("object") },
				{ QStringLiteral("properties"), QJsonObject{
					{ QStringLiteral("path"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("string") },
						{ QStringLiteral("description"), QStringLiteral("Relative directory path, default '.'.") }
					} },
					{ QStringLiteral("maxEntries"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("integer") },
						{ QStringLiteral("description"), QStringLiteral("Default 200, cap 2000.") }
					} }
				} }
			};
		}
		ToolResult execute(const QJsonObject& args, const ToolContext& ctx) override
		{
			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(QStringLiteral(".")), &error);
			if (path.isEmpty())
				return ToolResult::failure(error);

			const QDir dir(path);
			if (!dir.exists())
				return ToolResult::failure(QStringLiteral("not a directory: ")
					+ args.value(QStringLiteral("path")).toString());

			const int maxEntries = qBound(1, args.value(QStringLiteral("maxEntries")).toInt(200), 2000);
			const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
				QDir::DirsFirst | QDir::Name);

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
			return ToolResult::success(lines.join(QLatin1Char('\n')));
		}
	};

	class WriteFileTool : public UlinCoreTool
	{
	public:
		QString name() const override { return QStringLiteral("write_file"); }
		QString description() const override
		{
			return QStringLiteral("Write a UTF-8 text file inside the workspace. Disabled unless the host allows writes.");
		}
		QJsonObject parametersSchema() const override
		{
			return QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("object") },
				{ QStringLiteral("properties"), QJsonObject{
					{ QStringLiteral("path"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("string") },
						{ QStringLiteral("description"), QStringLiteral("Relative path from the workspace root.") }
					} },
					{ QStringLiteral("content"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("string") },
						{ QStringLiteral("description"), QStringLiteral("Full file content, UTF-8.") }
					} }
				} },
				{ QStringLiteral("required"), QJsonArray{
					QStringLiteral("path"),
					QStringLiteral("content")
				} }
			};
		}
		ToolResult execute(const QJsonObject& args, const ToolContext& ctx) override
		{
			if (!ctx.allowWrites)
				return ToolResult::failure(QStringLiteral("write_file is disabled (allowWrites=false)"));

			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString path = resolveInside(root, args.value(QStringLiteral("path")).toString(), &error);
			if (path.isEmpty())
				return ToolResult::failure(error);

			QDir().mkpath(QFileInfo(path).absolutePath());
			const QByteArray bytes = args.value(QStringLiteral("content")).toString().toUtf8();
			QFile file(path);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return ToolResult::failure(QStringLiteral("cannot write %1: %2")
					.arg(args.value(QStringLiteral("path")).toString(), file.errorString()));

			const qint64 written = file.write(bytes);
			file.close();
			if (written != bytes.size())
				return ToolResult::failure(QStringLiteral("short write: %1/%2").arg(written).arg(bytes.size()));

			const QString relative = relativeToRoot(root, path);
			return ToolResult::success(QStringLiteral("wrote %1 bytes to %2").arg(written).arg(relative),
				QJsonObject{
					{ QStringLiteral("path"), relative },
					{ QStringLiteral("bytes"), written }
				});
		}
	};

	class SearchTextTool : public UlinCoreTool
	{
	public:
		QString name() const override { return QStringLiteral("search_text"); }
		QString description() const override
		{
			return QStringLiteral("Search a literal string in text files inside the workspace.");
		}
		QJsonObject parametersSchema() const override
		{
			return QJsonObject{
				{ QStringLiteral("type"), QStringLiteral("object") },
				{ QStringLiteral("properties"), QJsonObject{
					{ QStringLiteral("query"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") } } },
					{ QStringLiteral("path"), QJsonObject{
						{ QStringLiteral("type"), QStringLiteral("string") },
						{ QStringLiteral("description"), QStringLiteral("Relative directory, default '.'.") }
					} },
					{ QStringLiteral("caseSensitive"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("boolean") } } },
					{ QStringLiteral("maxResults"), QJsonObject{ { QStringLiteral("type"), QStringLiteral("integer") } } }
				} },
				{ QStringLiteral("required"), QJsonArray{ QStringLiteral("query") } }
			};
		}
		ToolResult execute(const QJsonObject& args, const ToolContext& ctx) override
		{
			const QString query = args.value(QStringLiteral("query")).toString();
			if (query.isEmpty())
				return ToolResult::failure(QStringLiteral("query is empty"));

			const QString root = cleanRoot(ctx.workspaceRoot);
			QString error;
			const QString base = resolveInside(root, args.value(QStringLiteral("path")).toString(QStringLiteral(".")), &error);
			if (base.isEmpty())
				return ToolResult::failure(error);

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
						hits.append(QStringLiteral("%1:%2: %3")
							.arg(relativeToRoot(root, path))
							.arg(i + 1)
							.arg(lines.at(i).trimmed()));
					}
				}
			}

			return ToolResult::success(hits.isEmpty() ? QStringLiteral("no matches")
				: hits.join(QLatin1Char('\n')));
		}
	};
} // namespace

void registerBuiltinTools(UlinCoreToolRegistry& registry)
{
	registry.add(std::make_shared<ReadFileTool>());
	registry.add(std::make_shared<ListDirTool>());
	registry.add(std::make_shared<WriteFileTool>());
	registry.add(std::make_shared<SearchTextTool>());
}
} // namespace UlinCore
