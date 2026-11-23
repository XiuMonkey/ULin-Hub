#pragma once

// .ext 包（zip）的解析与安装，按描述文件里的 Type 分流。
// ClientExtension[Debug] → 客户端扩展：装进 <exe>/extensions/<Name>/，GUI 线程 QPluginLoader 装载；
// 其余 → 工具扩展：服务端扩展目录，Worker 线程按 JSON 协议调用。
//
// 描述文件分两个：info.json5 是身份与描述（Name / Description / Type / ABIVersion / ID / version /
// dependence / platform / tag，**所有扩展都有**）；regulation.json5 是注册表 Function[]（**只有工具扩展有**）。
// 读取规则：**info.json5 优先，缺了回退 regulation.json5**（见 infoPath），老包装上新宿主照旧能用；
// 新装的包一律落成新布局：客户端扩展只落 info.json5，工具扩展落 info.json5 + regulation.json5。
// Subtype 字段与 QQuickSupport 运行时包已退役：Qt Quick 随宿主部署，不再按 Subtype 做分流/预检。

#include <QString>
#include <QStringList>   // dependence 是 QStringList 成员（按值 ⇒ 必须是完整类型）

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

		// 界面契约版本（info 的 ABIVersion；未声明 = -1 ⇒ 视为匹配）；与 ClientInfo::kUiAbi 不符时只落 DLL、不落 ui/。
		int uiAbi = -1;
		// false = 声明的 ABI 与本宿主不符 ⇒ 本次没装扩展的 ui/（调用方据此给提示）
		bool uiAbiMatched = true;

		QString declaredId;
		// version：扩展**自身**版本（自由字符串，如 "0.1.0"），与 ABIVersion（界面契约版本）不同。
		QString declaredVersion;
		// dependence：依赖的其它扩展 ID 列表。非数组/非字符串项一律忽略（不报错、不拒绝整包）。
		QStringList dependencies;
		// tag：分类标签（自由词），供扩展管理/检索按类筛选；与 platform 同形，本版只登记、不参与判定。
		QStringList tags;
		// platform：适用平台，接受字符串（"windows"）或数组（["windows","linux"]）两种写法；空 = 未声明。
		QStringList platforms;
	};

	// 身份文件路径：<dir>/info.json5 优先，缺了回退 <dir>/regulation.json5（老布局），都没有返回空；安装、已安装扫描、卸载判据共用这一份规则。
	static QString infoPath(const QString& dir);

	// 注册表文件路径：<dir>/regulation.json5；没有则返回空（客户端扩展、或纯 DLL 无工具的扩展）；DllCaller 按它装载 Function[]。
	static QString regulationPath(const QString& dir);

	// 老布局就地补一份 info.json5，让磁盘布局统一；已有 info.json5 或连 regulation.json5 都没有时什么都不做（幂等，返回 true）。
	static bool ensureInfoJson(const QString& dir, QString* error = nullptr);

	// 只读"扩展身份"字段（ID / version / dependence / platform）：**尽力而为，不报错、不失败**，本版只登记、不参与装载判定。
	// 安装路径（readDescriptor）与启动扫描（ClientExtension::loadAll）共用这一份解析；传 infoPath()/regulationPath() 的路径即可。
	static void readIdentityFields(const QString& jsonPath, LoadedExtension* out);

	// 解包 → 找文件 → 读描述 → 按 Type 分流落地；成功时 out 带回落地路径与解析出的身份字段。
	bool loadAndInstall(const QString& extFilePath, const QString& serverProfilePath, LoadedExtension* out, QString* error = nullptr);

private:
	bool extractArchive(const QString& extFilePath, const QString& destDir, QString* error);
	bool findFiles(const QString& rootDir, LoadedExtension* out, QString* error);
	bool readDescriptor(const QString& jsonPath, LoadedExtension* out, QString* error);
	bool installPlugin(const QString& pluginPath, const QString& serverProfilePath, LoadedExtension* out, QString* error);

	QString m_errorString;
};
