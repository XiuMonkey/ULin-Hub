#pragma once

// .ext 包（zip）的解析与安装，按 regulation.json5 的 Type 分流。
// ClientExtension[Debug] → 客户端扩展：装进 <exe>/clientExtensions/<Name>/，GUI 线程 QPluginLoader 装载；
// 其余 → 工具扩展：服务端扩展目录，Worker 线程按 JSON 协议调用。
//
// （2026-10-01 删）regulation 里的 Subtype 字段与 QQuickSupport 运行时包已退役：宿主自己就是 QML 壳、
// Qt Quick 随宿主部署，不再需要 .ext 形式的运行时包，也不再按 Subtype 做分流/预检。

#include <QString>
#include <QStringList>   // 身份三件套里的 dependence（QStringList 成员，按值 ⇒ 必须是完整类型）

class ExtensionLoader
{
public:
	struct LoadedExtension
	{
		QString jsonPath;
		QString dllPath;
		QString pluginPath;
		QString pluginName;

		QString type;
		QString declaredName;

		// true = 客户端扩展路线：GUI 线程调 ClientExtension::loadOne，不碰服务端配置
		bool isClientExtension = false;

		// 扩展声明的界面契约版本（regulation 的 ABIVersion；未声明 = -1 ⇒ 视为匹配）。
		// 与宿主 kStylePackUiAbi 不符时只落 DLL、不落 ui/（见 loadAndInstall）。
		int uiAbi = -1;
		// false = 声明的 ABI 与本宿主不符 ⇒ 本次没装扩展的 ui/（调用方据此给提示）
		bool uiAbiMatched = true;

		// ---- 扩展身份三件套（2026-10-16 新增）----
		// ⚠️ 本版**只登记，不参与任何判定**：不校验依赖是否存在、不排序装载、不影响 ui/ 与 DLL。
		//    写在这里是为了让清单里的字段先有一个落点（日志能看到、后续版本再赋予语义）。
		// ID：标识符，约定**小写 + 下划线**（如 local_terminal）。与 Name（安装目录名，可含大小写）不是一回事；
		//     未声明时留空，由调用方按 Name 兜底。
		QString declaredId;
		// version：扩展**自身**版本（自由字符串，如 "0.1.0"）。注意与 ABIVersion（界面契约版本）不同。
		QString declaredVersion;
		// dependence：依赖的其它扩展 ID 列表。非数组/非字符串项一律忽略（不报错、不拒绝整包）。
		QStringList dependencies;
	};

	bool loadAndInstall(const QString& extFilePath, const QString& serverProfilePath, LoadedExtension* out, QString* error = nullptr);

private:
	bool extractArchive(const QString& extFilePath, const QString& destDir, QString* error);
	bool findFiles(const QString& rootDir, LoadedExtension* out, QString* error);
	bool readDescriptor(const QString& jsonPath, LoadedExtension* out, QString* error);
	bool installPlugin(const QString& pluginPath, const QString& serverProfilePath, LoadedExtension* out, QString* error);

	QString m_errorString;
};
