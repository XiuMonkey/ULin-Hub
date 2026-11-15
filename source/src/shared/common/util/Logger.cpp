#include "common/util/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <QtGlobal>

namespace {
	enum LogType   // 内部日志级别（与 logs/<module>/ 下的 5 个文件按序对应）；多出的 LogTypeCount 只当数组维数用
	{
		LogDebug = 0,
		LogInfo,
		LogWarning,
		LogError,
		LogFatal,
		LogTypeCount
	};

	const char* const kLevelNames[LogTypeCount] = { "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
	const char* const kLevelFileNames[LogTypeCount] = { "debug.log", "info.log", "warn.log", "error.log", "fatal.log" };

	LogType toLogType(QtMsgType type)
	{
		switch (type) {
		case QtDebugMsg: return LogDebug;
		case QtInfoMsg: return LogInfo;
		case QtWarningMsg: return LogWarning;
		case QtCriticalMsg: return LogError;
		case QtFatalMsg: return LogFatal;
		}
		return LogInfo;
	}

	struct ModuleRule   // 消息开头 [Tag]（或 Qt category）到模块文件夹名的映射
	{
		const char* tag;
		const char* folder;
	};

	const ModuleRule kModuleRules[] = {
		{ "History", "session" },   // 会话
		{ "MessageHost", "session" },
		{ "MessageQuery", "session" },
		{ "CacheManager", "session" },
		{ "SessionPrefetcher", "session" },
		{ "ModelSelector", "model" },   // 模型选择
		{ "ModelList", "model" },
		{ "ModelSelection", "model" },
		{ "Market", "plugin" },   // 插件市场
		{ "PluginMarketClient", "plugin" },
		{ "dsh-market-log", "plugin" },
		{ "market-installer", "plugin" },
		{ "ExtensionLoader", "extension" },   // 扩展管理
		{ "ExtensionManager", "extension" },
		{ "ExtensionRegistry", "extension" },
		{ "DllCaller", "extension" },
		{ "DSH DllCaller", "extension" },
		{ "ClientExtension", "extension" },   // 客户端扩展（装进本进程的 QPlugin DLL）：宿主侧装载器与插件自身各一个标签
		{ "QPlugin", "extension" },
		{ "ServerManager", "server" },   // 服务端 / 网络
		{ "DshApi", "server" },
		{ "DSH Server", "server" },
		{ "DSH Pipe", "server" },
		{ "DshNamedPipeBridge", "server" },
		{ "Translation", "ui" },   // UI / 界面
		{ "Theme", "ui" },
		{ "LayoutTrace", "ui" },
		{ "Render", "ui" },
		{ "ToolsFilter", "ui" },
		{ "Timing", "timing" },   // 耗时打点
		{ "ULin Hub", "app" },   // 应用整体
	};

	const int kRuleCount = static_cast<int>(sizeof(kModuleRules) / sizeof(kModuleRules[0]));
	const int kGeneralModuleIndex = kRuleCount; // 最后一个槽位给未识别的模块
	const int kModuleCount = kRuleCount + 1;

	struct ModuleTarget
	{
		const char* folder = nullptr;
		QFile* files[LogTypeCount] = {};
		QTextStream* streams[LogTypeCount] = {};
	};

	ModuleTarget g_modules[kModuleCount];
	QString g_logsDir;
	QMutex g_logMutex;

	// 单文件 8 MiB 封顶，保留 5 个历史分片（info.1.log … info.5.log，越新编号越小）。
	// 此前每个「模块×级别」是一个永不轮转的 Append 文件，extension/info.log 累积到 121 MB，
	// 打开/grep 都很慢；轮转后当前文件始终很小，最老的分片自动淘汰。
	constexpr qint64 kMaxLogFileBytes = 8 * 1024 * 1024;
	constexpr int kMaxLogBackups = 5;

	QString backupPath(const QString& path, int index)
	{
		const QString base = path.left(path.size() - 4);   // 去掉 ".log"
		return base + QStringLiteral(".") + QString::number(index) + QStringLiteral(".log");
	}

	bool needsRotate(const ModuleTarget* target, LogType level)
	{
		const QFile* file = target->files[level];
		return file && file->isOpen() && file->size() >= kMaxLogFileBytes;
	}

	void rotateLogFile(ModuleTarget* target, LogType level)
	{
		QFile* file = target->files[level];
		if (!file)
			return;
		const QString path = file->fileName();

		QFile::remove(backupPath(path, kMaxLogBackups));
		for (int i = kMaxLogBackups - 1; i >= 1; --i) {
			const QString from = backupPath(path, i);
			if (QFile::exists(from))
				QFile::rename(from, backupPath(path, i + 1));
		}

		if (target->streams[level])
			target->streams[level]->flush();
		delete target->streams[level];
		target->streams[level] = nullptr;
		file->close();

		// 外部程序占用导致改名失败时，退回原文件继续追加：宁可文件偏大也不丢日志。
		if (!QFile::rename(path, backupPath(path, 1))) {
			file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
			target->streams[level] = new QTextStream(file);
			return;
		}

		if (file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
			target->streams[level] = new QTextStream(file);
	}

	void openModuleTarget(ModuleTarget* target, const char* folder)
	{
		target->folder = folder;
		const QString dirPath = g_logsDir + QStringLiteral("/") + QString::fromUtf8(folder);
		if (!QDir().mkpath(dirPath))
			return;
		for (int i = 0; i < LogTypeCount; ++i) {
			const QString filePath = dirPath + QStringLiteral("/") + QString::fromUtf8(kLevelFileNames[i]);
			QFile* file = new QFile(filePath);
			if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
				delete file;
				continue;
			}

			target->files[i] = file;
			target->streams[i] = new QTextStream(file);
			// 启动时就把上次遗留的超大文件归档，当前文件从头开始，不用等写到阈值才切。
			if (needsRotate(target, static_cast<LogType>(i)))
				rotateLogFile(target, static_cast<LogType>(i));
		}
	}

	ModuleTarget* targetForFolder(const char* folder)
	{
		if (folder) {
			for (int i = 0; i < kModuleCount; ++i) {
				if (g_modules[i].folder && qstrcmp(g_modules[i].folder, folder) == 0)
					return &g_modules[i];
			}
		}
		return &g_modules[kGeneralModuleIndex];
	}

	const char* moduleFolderFor(const QMessageLogContext& context, const QString& message)   // 从消息开头的 [Tag]（找不到则尝试 Qt category）解析模块文件夹
	{
		if (message.startsWith(QLatin1Char('['))) {
			const int close = message.indexOf(QLatin1Char(']'));
			if (close > 1) {
				const QString tag = message.mid(1, close - 1);
				for (const ModuleRule& rule : kModuleRules) {
					if (tag.compare(QLatin1String(rule.tag), Qt::CaseSensitive) == 0)
						return rule.folder;
				}
			}
		}

		if (context.category) {
			const QString category = QString::fromUtf8(context.category);
			for (const ModuleRule& rule : kModuleRules) {
				if (category.compare(QLatin1String(rule.tag), Qt::CaseSensitive) == 0)
					return rule.folder;
			}
		}

		return nullptr;
	}

	void writeEntry(ModuleTarget* target, LogType level, const QMessageLogContext& context, const QString& message)
	{
		if (needsRotate(target, level))
			rotateLogFile(target, level);
		QTextStream* stream = target->streams[level];
		if (!stream)
			return;
		QString source;
		if (context.file) {
			source = QFileInfo(QString::fromUtf8(context.file)).fileName()
				+ QStringLiteral(":") + QString::number(context.line);
			if (context.function)
				source += QStringLiteral(" ") + QString::fromUtf8(context.function);
		}

		QString category;
		if (context.category)
			category = QString::fromUtf8(context.category);

		*stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
			<< QStringLiteral(" [") << QString::fromUtf8(target->folder) << QStringLiteral("] [")
			<< QLatin1String(kLevelNames[level]) << QStringLiteral("] ");

		if (!category.isEmpty())
			*stream << QStringLiteral("[") << category << QStringLiteral("] ");

		if (!source.isEmpty())
			*stream << source << QStringLiteral(": ");

		*stream << message << QStringLiteral("\n");

		// ⚠️ 必须显式 flush：QTextStream 写 "\n" 不会刷新缓冲区，而本模块的流是常驻的
		// （单例刻意不析构，退出时也不会析构），于是**单条日志会一直躺在缓冲里丢掉** ——
		// 表现是"某条 warn 明明该打却查不到"，只有刷屏级日志量才把缓冲撑满落盘。
		// 每行 flush 的代价远小于丢日志：这是排障通道，宁可慢一点也不许丢。
		stream->flush();
		stream->flush();
	}
}

static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	QMutexLocker locker(&g_logMutex);

	const LogType level = toLogType(type);
	const char* folder = moduleFolderFor(context, message);
	ModuleTarget* target = targetForFolder(folder);

	writeEntry(target, level, context, message);
}

void Logger::init()
{
	g_logsDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
	if (!QDir().mkpath(g_logsDir))
		return;
	for (int i = 0; i < kRuleCount; ++i)
		openModuleTarget(&g_modules[i], kModuleRules[i].folder);

	openModuleTarget(&g_modules[kGeneralModuleIndex], "general");

	qInstallMessageHandler(messageHandler);

	qInfo().noquote() << QStringLiteral("==== ULin Hub started ====")
		<< QStringLiteral("logs dir=") << g_logsDir;
}

QElapsedTimer& TimingLogger::timer()
{
	static QElapsedTimer t = []() {
		QElapsedTimer tm;
		tm.start();
		return tm;
	}();
	return t;
}

void TimingLogger::mark(const QString& phase)
{
	const qint64 totalMs = timer().nsecsElapsed() / 1000000;
	const qint64 deltaMs = totalMs - s_lastMs;
	qInfo().noquote() << QStringLiteral("[Timing] %1  +%2ms  (since start %3ms)")
		.arg(phase)
		.arg(deltaMs)
		.arg(totalMs);
	s_lastMs = totalMs;
}