#pragma once

// .ext 包（zip）的解析与安装，按描述文件里的 Type 分流。
// ClientExtension[Debug] → 客户端扩展：装进 <exe>/clientExtensions/<Name>/，GUI 线程 QPluginLoader 装载；
// 其余 → 工具扩展：服务端扩展目录，Worker 线程按 JSON 协议调用。
//
// ── 描述文件：一个还是两个，取决于扩展类型 ──────────────────────────────
//   info.json5        身份与描述：Name / Description / Type / ABIVersion / ID / version /
//                     dependence / platform / tag。**所有扩展都有**，客户端扩展只有它。
//   regulation.json5  注册表：Function[]（导出名 → 工具名、怎么调）。**只有工具扩展有**。
// 为什么要分开：客户端扩展的"regulation"里从来没有 Function[]，它只是一张身份卡；
// 工具扩展则是"身份 + 注册表"两件事挤在一个文件里 —— 拆开后各自的字段集才有唯一归属，
// 也不会再出现"改个版本号要动注册表"这种混账事。
//
// 兼容：老包只有一个 regulation.json5（身份和 Function[] 混在一起）。读取规则是
// **info.json5 优先，缺了回退 regulation.json5**（见 infoPath）——老包装上新宿主照旧能用，
// 只是日志里会提示是旧布局。新装的包一律落成新布局：客户端扩展落 info.json5；
// 工具扩展落 info.json5（老包从 regulation 抽出来补写）+ regulation.json5（原样）。
//
// regulation 里的 Subtype 字段与 QQuickSupport 运行时包已退役：宿主自己就是 QML 壳、
// Qt Quick 随宿主部署，不再需要 .ext 形式的运行时包，也不再按 Subtype 做分流/预检。

#include <QString>
#include <QStringList>   // 身份三件套里的 dependence（QStringList 成员，按值 ⇒ 必须是完整类型）

class ExtensionLoader
{
public:
	struct LoadedExtension
	{
		// 身份文件（info.json5；老包为 regulation.json5）——readDescriptor 读的就是它
		QString jsonPath;
		// 注册表文件（regulation.json5，只有工具扩展有；客户端扩展为空）
		QString regulationJsonPath;
		QString dllPath;
		QString pluginPath;
		QString pluginName;

		QString type;
		QString declaredName;

		// true = 客户端扩展路线：GUI 线程调 ClientExtension::loadOne，不碰服务端配置
		bool isClientExtension = false;

		// 扩展声明的界面契约版本（info 的 ABIVersion；未声明 = -1 ⇒ 视为匹配）。
		// 与宿主 ClientInfo::kUiAbi 不符时只落 DLL、不落 ui/（见 loadAndInstall）。
		int uiAbi = -1;
		// false = 声明的 ABI 与本宿主不符 ⇒ 本次没装扩展的 ui/（调用方据此给提示）
		bool uiAbiMatched = true;

		QString declaredId;
		// version：扩展**自身**版本（自由字符串，如 "0.1.0"）。注意与 ABIVersion（界面契约版本）不同。
		QString declaredVersion;
		// dependence：依赖的其它扩展 ID 列表。非数组/非字符串项一律忽略（不报错、不拒绝整包）。
		QStringList dependencies;
		// tag：分类标签（自由词，如 ["simulation","com"]），供扩展管理/检索按类筛选。
		// 与 platform 同形——单字符串或数组两种写法都认；本版只登记、不参与装载判定。
		QStringList tags;
		// platform：适用的平台（为跨平台做准备）。接受字符串（"windows"）或数组（["windows","linux"]）两种写法；
		// 空 = 未声明（本版视为"不限平台"，同样不参与判定）。
		QStringList platforms;
	};

	// 身份文件路径：<dir>/info.json5 优先；缺了回退 <dir>/regulation.json5（老布局）；都没有返回空。
	// 安装（readDescriptor）、已安装扫描（ClientExtension）、卸载判据共用这一个函数 —— 文件名的规则只有一份。
	static QString infoPath(const QString& dir);

	// 注册表文件路径：<dir>/regulation.json5；没有则返回空（客户端扩展、或纯 DLL 无工具的扩展）。
	// DllCaller 按它装载 Function[]，ExtensionDllLoader 按它扫描。
	static QString regulationPath(const QString& dir);

	// 老布局（只有 regulation.json5、身份混在里面）就地补一份 info.json5，让磁盘布局统一；
	// 已有 info.json5 或连 regulation.json5 都没有时什么都不做（幂等，返回 true）。
	static bool ensureInfoJson(const QString& dir, QString* error = nullptr);

	// 只读"扩展身份"字段（ID / version / dependence / platform）：**尽力而为，不报错、不失败**。
	// 安装路径（readDescriptor）与启动扫描（ClientExtension::loadAll 扫已安装扩展）共用这一份解析，
	// 免得两处各写一份（解析规则一改就漏一处）。本版这些字段只登记、不参与任何装载判定。
	// 传 infoPath()/regulationPath() 得到的路径即可（老包两者同一个文件）。
	static void readIdentityFields(const QString& jsonPath, LoadedExtension* out);

	bool loadAndInstall(const QString& extFilePath, const QString& serverProfilePath, LoadedExtension* out, QString* error = nullptr);

private:
	bool extractArchive(const QString& extFilePath, const QString& destDir, QString* error);
	bool findFiles(const QString& rootDir, LoadedExtension* out, QString* error);
	bool readDescriptor(const QString& jsonPath, LoadedExtension* out, QString* error);
	bool installPlugin(const QString& pluginPath, const QString& serverProfilePath, LoadedExtension* out, QString* error);

	QString m_errorString;
};
