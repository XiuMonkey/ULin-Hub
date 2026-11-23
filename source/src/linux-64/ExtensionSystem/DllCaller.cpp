// JSON5 DLL 描述解析 + DLL 调用（json / native 两种风格；com 走 ComCaller）。

#include "ExtensionSystem/DllCaller.h"
#include "ExtensionSystem/Thunk.h"
#include "ExtensionSystem/ComCaller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <cstdint>
#include <utility>

namespace
{
	// 规范化 DLL 路径：优先真实路径（解开符号链接），文件还不存在时退回绝对路径，最后清掉 . / ..
	QString normalizedDllPath(const QString& path)
	{
		const QFileInfo info(path);
		const QString canonical = info.canonicalFilePath();
		const QString absolute = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
		return QDir::cleanPath(absolute);
	}
}

DllCaller::DllCaller() = default;

DllCaller::~DllCaller()
{
	unloadLibrary();
}

// 读入一份 JSON5 描述并整份入表：解析 Function[]、把 LoadingSource 固化成绝对路径、校验扩展名与工具名
// 唯一。解析结果先放局部副本，失败分支不触碰已加载的扩展。
bool DllCaller::loadDescriptor(const QString& json5Path)
{
	QMutexLocker stateLock(&m_mutex); // 全流程持锁：并发加载同名扩展只能有一个成功

	QFile file(json5Path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		m_errorString = QStringLiteral("cannot open descriptor: %1").arg(json5Path);
		return false;
	}

	const QByteArray raw = file.readAll(); // 按字节读：注释与尾逗号预处理都是字节级扫描
	file.close();

	Descriptor parsed; // 局部副本：校验全部通过后才会 move 进 m_extensions
	QString error;
	if (!parseDescriptor(raw, &parsed, &error)) {
		m_errorString = error;
		return false;
	}

	const QString dir = QFileInfo(json5Path).absolutePath(); // LoadingSource 相对路径的基准目录
	QString name = QFileInfo(dir).fileName(); // 扩展名 = 描述文件所在目录的 basename
	if (name.isEmpty())
		name = QFileInfo(json5Path).baseName();

	for (const LoadedExtension& ext : m_extensions) { // 同名扩展已加载则拒绝添加
		if (ext.name.compare(name, Qt::CaseInsensitive) == 0) {
			m_errorString = QStringLiteral("extension already loaded: %1 (remove it first)").arg(name);
			return false;
		}
	}

	for (FunctionSpec& fn : parsed.functions) { // LoadingSource 一律解析成绝对路径，多扩展同名 main.so 互不冲突
		// 兼容仍写 main.dll 的旧描述文件；Linux 实际文件是 main.so。
		// 只在同名 .so 确实存在时才改（缺文件仍按原路径报错，错误信息保留原写法）。
		if (fn.loadingSource.endsWith(QStringLiteral(".dll"))) {
			QString so = fn.loadingSource;
			so.chop(4);
			so += QStringLiteral(".so");
			if (QFileInfo(QDir(dir).filePath(so)).exists())
				fn.loadingSource = so;
		}
		fn.resolvedDllPath = normalizedDllPath(QDir(dir).filePath(fn.loadingSource));
	}

	QSet<QString> occupied; // 同名工具直接拒绝
	for (const LoadedExtension& ext : m_extensions) {
		for (const FunctionSpec& fn : ext.descriptor.functions) {
			if (!fn.tool.isEmpty())
				occupied.insert(fn.tool);
		}
	}
	QSet<QString> selfSeen; // 同一描述内重复声明同名工具也要拒（跨扩展查重用上面的 occupied）
	for (const FunctionSpec& fn : parsed.functions) {
		if (fn.tool.isEmpty())
			continue;
		if (selfSeen.contains(fn.tool)) {
			m_errorString = QStringLiteral("descriptor %1 registers tool \"%2\" more than once").arg(name, fn.tool);
			return false;
		}
		selfSeen.insert(fn.tool);
		if (occupied.contains(fn.tool)) {
			m_errorString = QStringLiteral("tool \"%1\" already registered by another extension").arg(fn.tool);
			return false;
		}
	}

	// 提交点：前面的校验都已通过，这里才真正改 m_extensions
	LoadedExtension entry;
	entry.name = name;
	entry.descriptor = std::move(parsed); // move 入表：描述符只此一份
	m_extensions.append(std::move(entry));

	qInfo().noquote() << QStringLiteral("[DllCaller] descriptor loaded: %1 name=%2 functions=%3")
		.arg(json5Path, name).arg(m_extensions.last().descriptor.functions.size());
	return true;
}

// 预加载入口：只保证库句柄可用，不登记扩展（描述符走 loadDescriptor）。
bool DllCaller::loadLibrary(const QString& dllPath)
{
	return libraryForPath(dllPath) != nullptr;
}

// 整体卸载：清空描述符 → 等每个库的在途调用归零 → unload 并 delete 条目。析构、reload 与工具宿主停止时调用。
void DllCaller::unloadLibrary()
{
	QMutexLocker stateLock(&m_mutex);

	m_extensions.clear(); // 先清描述符再等：否则 delete 前可能又冒出新的调用

	const QStringList paths = m_librariesByPath.keys(); // 先收集路径再逐个等：wait 会放开 m_mutex
	for (const QString& key : paths) {
		LoadedLibrary* entry = m_librariesByPath.value(key);
		if (!entry)
			continue;
		while (entry->inFlight > 0)
			entry->drained.wait(&m_mutex);
		if (m_librariesByPath.value(key) != entry)
			continue;
		m_librariesByPath.remove(key);
		if (entry->library) { // unload 只减引用；QLibrary 对象本身还要 delete
			entry->library->unload();
			delete entry->library;
		}
		delete entry;
	}
	m_errorString.clear(); // 不 clear m_librariesByPath：被 continue 跳过的条目会丢指针
}

// 先摘描述符，再卸掉只属于它的 DLL。摘条目前必须等在途调用归零（inFlight==0 且 runMutex 已释放）：
// 顺序反了的话，在途调用的 libraryForPath() 查不到条目会重新加载同一 DLL、拿到新的 runMutex，
// 串行保证当场失效。
bool DllCaller::removeExtension(const QString& name)
{
	QMutexLocker stateLock(&m_mutex);

	int index = -1;
	for (int i = 0; i < m_extensions.size(); ++i) {
		if (m_extensions.at(i).name.compare(name, Qt::CaseInsensitive) == 0) { // 大小写不敏感：与 Windows 版一致
			index = i;
			break;
		}
	}

	if (index < 0) {
		m_errorString = QStringLiteral("extension not loaded: %1").arg(name);
		return false;
	}

	m_extensions.removeAt(index); // 先摘描述符：此后 callTool 查不到这条，不会再有新调用进来

	QSet<QString> keep; // 剩下扩展仍引用的 DLL 路径（规范化后）
	for (const LoadedExtension& ext : m_extensions) {
		for (const FunctionSpec& fn : ext.descriptor.functions) {
			if (!fn.resolvedDllPath.isEmpty())
				keep.insert(fn.resolvedDllPath);
		}
	}

	QStringList drop; // 卸载仅属于该扩展的 DLL：等调用结束以释放文件占用
	for (auto it = m_librariesByPath.constBegin(); it != m_librariesByPath.constEnd(); ++it) {
		if (!keep.contains(it.key()))
			drop.append(it.key());
	}
	for (const QString& key : drop) {
		LoadedLibrary* entry = m_librariesByPath.value(key);
		if (!entry)
			continue;

		while (entry->inFlight > 0)
			entry->drained.wait(&m_mutex); // 先归零、再摘条目（顺序理由见函数头）

		if (m_librariesByPath.value(key) != entry) // wait 期间 QHash 可能重哈希，重新确认条目没被换掉
			continue;

		m_librariesByPath.remove(key); // inFlight 已归零且 runMutex 已释放，摘除安全
		if (entry->library) {
			entry->library->unload();
			delete entry->library;
		}
		delete entry;
	}

	qInfo().noquote() << QStringLiteral("[DllCaller] extension removed: %1").arg(name);
	return true;
}

QLibrary* DllCaller::libraryForPath(const QString& absoluteDllPath)
{
	QMutexLocker stateLock(&m_mutex); // ensureRuntimeLocked 要求调用方持锁
	LoadedLibrary* entry = ensureRuntimeLocked(absoluteDllPath);
	return entry ? entry->library : nullptr;
}

// 调用方须已持有 m_mutex。只有导出 ClearMem 的 DLL 才要归还堆内存，无该导出的老扩展保持 static 缓冲。
DllCaller::LoadedLibrary* DllCaller::ensureRuntimeLocked(const QString& absoluteDllPath)
{
	const QString key = normalizedDllPath(absoluteDllPath); // 以规范化路径为键：同一 DLL 的多种写法共用一份句柄
	auto it = m_librariesByPath.constFind(key);
	if (it != m_librariesByPath.constEnd())
		return it.value();

	// 空路径与文件缺失都算加载失败，原因写进 m_errorString
	if (key.isEmpty()) {
		m_errorString = QStringLiteral("empty DLL path");
		return nullptr;
	}

	if (!QFile::exists(key)) {
		m_errorString = QStringLiteral("cannot find DLL: %1").arg(key);
		return nullptr;
	}

	auto* entry = new LoadedLibrary;
	entry->library = new QLibrary(key); // 条目所有权归 m_librariesByPath，卸载时统一 unload + delete
	entry->library->setLoadHints(QLibrary::ResolveAllSymbolsHint); // 加载期解析全部符号：缺符号当场失败
	if (!entry->library->load()) {
		m_errorString = QStringLiteral("cannot load DLL: %1 (%2)").arg(key, entry->library->errorString());
		delete entry->library;
		delete entry;
		return nullptr;
	}

	entry->clearMem = reinterpret_cast<ClearMemFn>(entry->library->resolve("ClearMem")); // 可选导出：没有则结果不归调用方释放

	m_librariesByPath.insert(key, entry);
	qInfo().noquote() << "[DllCaller] library loaded:" << key;
	return entry;
}

// 记录最后一次错误：写 m_errorString（供 errorString() 读）并回填调用方的 out 参数。
void DllCaller::recordError(QString* out, const QString& message)
{
	QMutexLocker stateLock(&m_mutex);
	m_errorString = message;
	if (out)
		*out = message;
}

// 结果内存归还：按 fn 的 resolvedDllPath 找对应 DLL 的 ClearMem，找不到就按老约定跳过。
void DllCaller::releaseResult(const FunctionSpec& fn, const char* raw)
{
	if (!raw)
		return;
	QMutexLocker stateLock(&m_mutex);
	auto it = m_librariesByPath.constFind(normalizedDllPath(fn.resolvedDllPath));
	if (it == m_librariesByPath.constEnd() || !it.value() || !it.value()->clearMem)
		return; // 未导出 ClearMem（旧约定），不释放
	it.value()->clearMem(raw);
}

// 汇总所有已加载扩展登记的工具名，供工具宿主对外发布与日志使用。
QStringList DllCaller::tools() const
{
	QMutexLocker stateLock(&m_mutex);

	QStringList result;
	for (const LoadedExtension& ext : m_extensions) {
		for (const FunctionSpec& fn : ext.descriptor.functions) {
			if (!fn.tool.isEmpty())
				result.append(fn.tool);
		}
	}
	return result;
}

QString DllCaller::errorString() const
{
	QMutexLocker stateLock(&m_mutex);
	return m_errorString;
}

// 按 tool 名在已加载扩展里找配方并执行。执行完必须先释放 runMutex、再递减 inFlight：卸载方等到
// inFlight 归零会连 runMutex 一起 delete 该 LoadedLibrary，反了的话 ~QMutexLocker 会 unlock 已
// delete 的 QMutex。
bool DllCaller::callTool(const QString& tool, const QJsonObject& args, QJsonObject& result, QString* errorMessage)
{
	QString fallbackError;
	QString* errOut = errorMessage ? errorMessage : &fallbackError; // 错误统一走 out 参数

	FunctionSpec spec; // 锁内只拷一份配方：执行阶段不持 m_mutex，卸载/新增不会与本次调用抢锁
	bool found = false;
	{
		QMutexLocker stateLock(&m_mutex);
		for (const LoadedExtension& ext : m_extensions) {
			for (const FunctionSpec& fn : ext.descriptor.functions) {
				if (fn.tool == tool) {
					spec = fn;
					found = true;
					break;
				}
			}
			if (found)
				break;
		}
	}

	if (!found) {
		// 未登记的 tool：result 保持调用方传入时的内容，只报错
		qWarning().noquote() << "[DllCaller] unknown tool:" << tool;
		recordError(errOut, QStringLiteral("unknown tool: %1").arg(tool));
		return false;
	}

	// 描述里省略 InterfaceType 时按 default（DLL 直调）处理
	const QString interfaceType = spec.interfaceType.isEmpty()
		? QStringLiteral("default")
		: spec.interfaceType;

	// 分支覆盖三种情况：default 走 DLL、com 委托 ComCaller、http/websocket 尚未实现
	if (interfaceType == QStringLiteral("default")) {
		LoadedLibrary* entry = nullptr; // 同一 DLL 经 runMutex 串行、不同 DLL 并行；卸载等 inFlight 归零
		{
			QMutexLocker stateLock(&m_mutex);
			entry = ensureRuntimeLocked(spec.resolvedDllPath);
			if (entry)
				++entry->inFlight;
		}

		if (!entry) {
			// ensureRuntimeLocked 已把失败原因写进 m_errorString
			qWarning().noquote() << "[DllCaller] DLL not loaded, tool=" << tool
				<< " source=" << spec.loadingSource;
			recordError(errOut, QStringLiteral("DLL not loaded: %1").arg(spec.loadingSource));
			return false;
		}

		bool invoked = false; // 失败原因统一走 errOut，不用异常或返回码区分
		{
			QMutexLocker runLock(&entry->runMutex);
			// style 决定走 JSON 字符串协议还是 native ABI thunk
			if (spec.style == QStringLiteral("json")) {
				invoked = invokeJsonFunction(spec, args, result, errOut);
			}
			else if (spec.style == QStringLiteral("native")) {
				invoked = invokeNativeFunction(spec, args, result, errOut);
			}
			else {
				qWarning().noquote() << "[DllCaller] unsupported calling style:" << spec.style;
				recordError(errOut, QStringLiteral("unsupported calling style: %1").arg(spec.style));
			}
		}

		{
			QMutexLocker stateLock(&m_mutex);
			if (--entry->inFlight == 0) // 顺序理由见函数头：必须已释放 runMutex 才能递减
				entry->drained.wakeAll();
		}

		if (!invoked) {
			qWarning().noquote() << "[DllCaller] tool call failed tool=" << tool
				<< " error=" << *errOut;
			return false;
		}

		qInfo().noquote() << "[DllCaller] tool call succeeded tool=" << tool;
		return true;
	}
	else if (interfaceType == QStringLiteral("http")) {
		// 协议预留：http / websocket 只明确报错，避免调用方拿到空结果
		qWarning().noquote() << "[DllCaller] interfaceType \"http\" not implemented yet, tool=" << tool;
		recordError(errOut, QStringLiteral("interfaceType \"http\" not implemented yet"));
		return false;
	}
	else if (interfaceType == QStringLiteral("websocket")) {
		qWarning().noquote() << "[DllCaller] interfaceType \"websocket\" not implemented yet, tool=" << tool;
		recordError(errOut, QStringLiteral("interfaceType \"websocket\" not implemented yet"));
		return false;
	}
	else if (interfaceType == QStringLiteral("com")) {
		if (!spec.comConfig.isEmpty()) { // COM 由 ComCaller 执行（白名单在 "Com" 段声明）：每次新建实例、可跨线程并行
			QString comError;
			if (!comcall::invoke(spec.comConfig, args, result, comError)) {
				recordError(errOut, comError);
				return false;
			}
			qInfo().noquote() << "[DllCaller] tool call succeeded tool=" << tool;
			return true;
		}

		qWarning().noquote() << "[DllCaller] com tool lacks Com config, tool=" << tool;
		recordError(errOut, qtTrId("com_missing_description"));
		return false;
	}

	// 未知 interfaceType：描述文件写错或用了没实现的接口
	qWarning().noquote() << "[DllCaller] unsupported interfaceType, tool=" << tool
		<< " interfaceType=" << interfaceType;
	recordError(errOut, QStringLiteral("unsupported interfaceType: %1 (supported: \"default\")").arg(interfaceType));
	return false;
}

bool DllCaller::parseDescriptor(const QByteArray& json5, Descriptor* out, QString* error)
{
	// JSON5 只做两项预处理（剥注释、去尾逗号），其余语法仍按严格 JSON 解析
	const QByteArray cleaned = removeTrailingCommas(stripJson5Comments(json5));

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(cleaned, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		*error = QStringLiteral("JSON5 parse error: %1 at offset %2").arg(parseError.errorString())
			.arg(parseError.offset);
		return false;
	}

	const QJsonObject root = doc.object();

	// 顶层只认 Function 段；身份信息在 info.json5，不归本类管
	const QJsonArray functions = root.value(QStringLiteral("Function")).toArray();
	if (functions.isEmpty()) {
		*error = QStringLiteral("descriptor has no Function array");
		return false;
	}

	out->functions.clear();
	for (const QJsonValue& value : functions) {
		const QJsonObject obj = value.toObject();
		FunctionSpec fn;
		fn.function = obj.value(QStringLiteral("Func")).toString();
		fn.tool = obj.value(QStringLiteral("Tool")).toString();
		fn.loadingSource = obj.value(QStringLiteral("LoadingSource")).toString(QStringLiteral("main.so"));
		fn.interfaceType = obj.value(QStringLiteral("InterfaceType")).toString(QStringLiteral("default"));

		if (fn.interfaceType != QStringLiteral("default")) // 非 default 接口的配置段；ProgId 固定（白名单式）
			fn.comConfig = obj.value(QStringLiteral("Com")).toObject();

		// Calling Convention 段：Style 选调用方式，ReturnType 选桥接签名/ABI 返回槽
		const QJsonObject cc = obj.value(QStringLiteral("Calling Convention")).toObject();
		fn.style = cc.value(QStringLiteral("Style")).toString(QStringLiteral("json"));
		fn.returnType = cc.value(QStringLiteral("ReturnType")).toString(QStringLiteral("int"));
		fn.resultIsJson = cc.value(QStringLiteral("ResultIsJson")).toBool(true); // 默认 true：DLL 直接把结果当 JSON 对象回

		const QJsonArray params = cc.value(QStringLiteral("Parameters")) // Parameters 为准，兼容旧名 Params
			.toArray(cc.value(QStringLiteral("Params")).toArray());
		for (const QJsonValue& paramValue : params) {
			const QJsonObject paramObj = paramValue.toObject();
			ParameterSpec param; // 字段名大小写都认：老描述文件写小写（name/type/from）
			param.name = paramObj.value(QStringLiteral("Name"))
				.toString(paramObj.value(QStringLiteral("name")).toString());
			param.type = paramObj.value(QStringLiteral("Type"))
				.toString(paramObj.value(QStringLiteral("type")).toString());
			param.from = paramObj.value(QStringLiteral("From")) // 从 args 的哪个键取值，缺省用 Name
				.toString(paramObj.value(QStringLiteral("from")).toString());
			param.defaultValue = paramObj.value(QStringLiteral("Default")); // 缺参时的兜底值
			fn.parameters.append(param);
		}

		// Tool 对所有接口必需；Func 仅 default（DLL）接口需要
		if (fn.tool.isEmpty()
			|| (fn.interfaceType == QStringLiteral("default") && fn.function.isEmpty())) {
			*error = qtTrId("com_function_requires_tool");
			return false;
		}

		out->functions.append(fn);
	}

	return true;
}

// JSON 风格调用：args 序列化成紧凑 JSON 字符串传入，结果按字符串取回。
// 两种导出签名按 ReturnType 区分：const char*(const char*) 与 int/void(const char*, char**)。
bool DllCaller::invokeJsonFunction(const FunctionSpec& fn, const QJsonObject& args, QJsonObject& result, QString* error)
{
	if (fn.style != QStringLiteral("json")) {
		recordError(error, QStringLiteral("unsupported calling style: %1 (only \"json\" is implemented)")
			.arg(fn.style));
		return false;
	}

	const QByteArray argsJson = QJsonDocument(args).toJson(QJsonDocument::Compact); // 紧凑格式：减小传给 DLL 的缓冲

	const auto applyResult = [&](const QByteArray& bytes) {   // 结果落地：resultIsJson 时 DLL 回的是 JSON 对象，否则包成 { "value": "<原文>" }
		if (fn.resultIsJson) {
			QJsonParseError parseError;
			const QJsonDocument resultDoc = QJsonDocument::fromJson(bytes, &parseError);
			if (parseError.error != QJsonParseError::NoError || !resultDoc.isObject()) {
				recordError(error, QStringLiteral("DLL result is not a JSON object: %1").arg(fn.function));
				return false;
			}
			result = resultDoc.object();
		}
		else {
			result.insert(QStringLiteral("value"), QString::fromUtf8(bytes));
		}
		return true;
	};

	QLibrary* library = libraryForPath(fn.resolvedDllPath); // DLL 缺失 / 加载失败时返回 nullptr
	if (!library) {
		recordError(error, QStringLiteral("DLL not loaded for function %1: %2").arg(fn.function, errorString()));
		return false;
	}

	if (fn.returnType == QStringLiteral("string")) { // 方式一：const char* Func(const char*)
		using StringFn = const char* (*)(const char*);
		auto* symbol = reinterpret_cast<StringFn>(library->resolve(fn.function.toUtf8().constData()));
		if (!symbol) {
			recordError(error, QStringLiteral("cannot resolve function: %1 (%2)")
				.arg(fn.function, library->errorString()));
			return false;
		}

		const char* rawResult = symbol(argsJson.constData());
		if (!rawResult) {
			recordError(error, QStringLiteral("DLL function returned null: %1").arg(fn.function));
			return false;
		}

		const QByteArray resultBytes(rawResult); // 内存契约：导出 ClearMem 时须先拷出、再立刻归还
		releaseResult(fn, rawResult);

		return applyResult(resultBytes);
	}

	using IntFn = int (*)(const char*, char**); // 方式二：int/void Func(const char*, char**)
	using VoidFn = void (*)(const char*, char**);

	char* resultPtr = nullptr; // 出参：由 DLL 写入（导出 ClearMem 时也是它分配），调用后非空才算成功

	if (fn.returnType == QStringLiteral("void")) {
		auto* symbol = reinterpret_cast<VoidFn>(library->resolve(fn.function.toUtf8().constData()));
		if (!symbol) {
			recordError(error, QStringLiteral("cannot resolve function: %1 (%2)")
				.arg(fn.function, library->errorString()));
			return false;
		}
		symbol(argsJson.constData(), &resultPtr);
	}
	else {
		auto* symbol = reinterpret_cast<IntFn>(library->resolve(fn.function.toUtf8().constData()));
		if (!symbol) {
			recordError(error, QStringLiteral("cannot resolve function: %1 (%2)")
				.arg(fn.function, library->errorString()));
			return false;
		}

		const int code = symbol(argsJson.constData(), &resultPtr); // 非 0 即失败，此时 resultPtr 内容不可用
		if (code != 0) {
			recordError(error, QStringLiteral("DLL function returned error code %1: %2").arg(code).arg(fn.function));
			return false;
		}
	}

	if (!resultPtr) { // void 签名也必须写结果；没写就是 DLL 违约
		recordError(error, QStringLiteral("DLL function did not write a result: %1").arg(fn.function));
		return false;
	}

	// 字符串结果的生命周期只到 ClearMem 调用为止，先拷进 QByteArray
	const QByteArray resultBytes(resultPtr);
	releaseResult(fn, resultPtr); // 拷走后立即归还堆内存

	return applyResult(resultBytes);
}

// native 风格调用：按 Parameters/ReturnType 现生成 Thunk，用真实 ABI 直接调导出函数，不要求 DLL 遵守 JSON 协议。
bool DllCaller::invokeNativeFunction(const FunctionSpec& fn, const QJsonObject& args, QJsonObject& result, QString* error)
{
	Thunk::Signature signature;

	// ReturnType 映射到 thunk 的返回槽；未列出的类型一律按 int 处理
	if (fn.returnType == QStringLiteral("void"))
		signature.returnType = Thunk::ReturnType::Void;
	else if (fn.returnType == QStringLiteral("bool"))
		signature.returnType = Thunk::ReturnType::Bool;
	else if (fn.returnType == QStringLiteral("double"))
		signature.returnType = Thunk::ReturnType::Double;
	else if (fn.returnType == QStringLiteral("string")
		|| fn.returnType == QStringLiteral("const char*")
		|| fn.returnType == QStringLiteral("char*"))
		signature.returnType = Thunk::ReturnType::String;
	else
		signature.returnType = Thunk::ReturnType::Int;

	if (fn.parameters.size() > 16) { // Thunk 生成器上限 16 个参数，这里先拦下给出直接错误
		recordError(error, QStringLiteral("native caller currently supports at most 16 parameters"));
		return false;
	}

	QVector<Thunk::Arg> nativeArgs;
	nativeArgs.resize(fn.parameters.size());
	QVector<QByteArray> stringStorage;
	stringStorage.reserve(fn.parameters.size());

	// 按描述把 args 里的值填进统一的 8 字节参数槽；字符串先落到 stringStorage 里保活，槽里只存指针
	for (int i = 0; i < fn.parameters.size(); ++i) {
		const ParameterSpec& param = fn.parameters.at(i);
		const QString& type = param.type;

		Thunk::ArgType argType;
		if (type == QStringLiteral("bool")) {
			argType = Thunk::ArgType::Bool;
		}
		else if (type == QStringLiteral("int")) {
			argType = Thunk::ArgType::Int;
		}
		else if (type == QStringLiteral("double")) {
			argType = Thunk::ArgType::Double;
		}
		else if (type == QStringLiteral("string") || type == QStringLiteral("const char*") ||
			type == QStringLiteral("char*")) {
			argType = Thunk::ArgType::String;
		}
		else if (type == QStringLiteral("pointer32") || type == QStringLiteral("ptr32")) {
			argType = Thunk::ArgType::Pointer32;
		}
		else if (type == QStringLiteral("pointer64") || type == QStringLiteral("ptr64")) {
			argType = Thunk::ArgType::Pointer64;
		}
		else {
			recordError(error, QStringLiteral("unsupported native parameter type: %1").arg(type));
			return false;
		}

		signature.args.append(argType);

		const QString key = param.from.isEmpty() ? param.name : param.from; // From 优先：可把同一入参换名传给 DLL
		QJsonValue value = args.value(key);
		if (value.isUndefined() && !param.defaultValue.isUndefined())
			value = param.defaultValue;

		Thunk::Arg& native = nativeArgs[i]; // 按目标类型把 JSON 值写进参数槽
		switch (argType) {
		case Thunk::ArgType::Bool:
		case Thunk::ArgType::Int:
			native.as.i = value.toInt();
			break;
		case Thunk::ArgType::Double:
			native.as.d = value.toDouble();
			break;
		case Thunk::ArgType::String:
		case Thunk::ArgType::Pointer64:
			stringStorage.append(value.toString().toUtf8());
			native.as.s = stringStorage.last().constData();
			break;
		case Thunk::ArgType::Pointer32:
			native.as.u32 = static_cast<std::uint32_t>(value.toDouble());
			break;
		}
	}

	QLibrary* library = libraryForPath(fn.resolvedDllPath); // 同 invokeJsonFunction：取一次句柄并判空
	if (!library) {
		recordError(error, QStringLiteral("DLL not loaded for function %1: %2").arg(fn.function, errorString()));
		return false;
	}

	auto* symbol = library->resolve(fn.function.toUtf8().constData()); // 先取裸地址，再交给 Thunk 生成调用代码
	if (!symbol) {
		recordError(error, QStringLiteral("cannot resolve function: %1 (%2)")
			.arg(fn.function, library->errorString()));
		return false;
	}

	Thunk::Thunk thunk;
	if (!thunk.build(signature, reinterpret_cast<void*>(symbol))) { // 生成机器码失败，原因在 thunk 内
		recordError(error, thunk.errorString());
		return false;
	}

	// 结果按返回槽类型取出：Void 只回 ok，其余统一塞进 value
	switch (signature.returnType) {
	case Thunk::ReturnType::Void: {
		int dummy = 0;
		thunk.call(nativeArgs.constData(), &dummy);
		result.insert(QStringLiteral("ok"), true);
		break;
	}
	case Thunk::ReturnType::Bool: {
		bool value = false;
		thunk.call(nativeArgs.constData(), &value);
		result.insert(QStringLiteral("value"), value);
		break;
	}
	case Thunk::ReturnType::Int: {
		int value = 0;
		thunk.call(nativeArgs.constData(), &value);
		result.insert(QStringLiteral("value"), value);
		break;
	}
	case Thunk::ReturnType::Double: {
		double value = 0.0;
		thunk.call(nativeArgs.constData(), &value);
		result.insert(QStringLiteral("value"), value);
		break;
	}
	case Thunk::ReturnType::String: {
		const char* value = nullptr;
		thunk.call(nativeArgs.constData(), &value);
		result.insert(QStringLiteral("value"), QString::fromUtf8(value ? value : ""));
		break;
	}
	}

	return true;
}

QByteArray DllCaller::stripJson5Comments(const QByteArray& input)
{
	// 逐字节扫描，只在字符串外识别注释；字符串里的 // 与 /* 原样保留
	QByteArray output;
	output.reserve(input.size());

	bool inString = false;
	bool escape = false;
	int i = 0;
	const int n = input.size();

	while (i < n) {
		const char c = input.at(i);

		if (inString) {
			output.append(c);
			if (escape) {
				escape = false;
			}
			else if (c == '\\') {
				escape = true;
			}
			else if (c == '"') {
				inString = false;
			}
			++i;
			continue;
		}

		if (c == '"') {
			inString = true;
			output.append(c);
			++i;
			continue;
		}

		if (c == '/' && i + 1 < n && input.at(i + 1) == '/') {
			while (i < n && input.at(i) != '\n')
				++i;
			continue;
		}

		if (c == '/' && i + 1 < n && input.at(i + 1) == '*') {
			i += 2;
			while (i + 1 < n && !(input.at(i) == '*' && input.at(i + 1) == '/'))
				++i;
			i += 2;
			continue;
		}

		output.append(c);
		++i;
	}

	return output;
}

// JSON5 预处理之二：删掉对象/数组末尾的悬空逗号（,} / ,]），换成空格顶位。
QByteArray DllCaller::removeTrailingCommas(const QByteArray& input)
{
	// 扫描状态与 stripJson5Comments 同构：字符串内的逗号不处理
	QByteArray output;
	output.reserve(input.size());

	bool inString = false;
	bool escape = false;
	int i = 0;
	const int n = input.size();

	while (i < n) {
		const char c = input.at(i);

		if (inString) {
			output.append(c);
			if (escape) {
				escape = false;
			}
			else if (c == '\\') {
				escape = true;
			}
			else if (c == '"') {
				inString = false;
			}
			++i;
			continue;
		}

		if (c == '"') {
			inString = true;
			output.append(c);
			++i;
			continue;
		}

		if (c == ',') { // 逗号后跳过空白，看下一个非空白字符是不是 } 或 ]
			int j = i + 1;
			while (j < n && (input.at(j) == ' ' || input.at(j) == '\t' || input.at(j) == '\r' || input.at(j) == '\n'))
				++j;
			if (j < n && (input.at(j) == '}' || input.at(j) == ']')) {
				output.append(' ');
				i = j;
				continue;
			}
		}

		output.append(c);
		++i;
	}

	return output;
}