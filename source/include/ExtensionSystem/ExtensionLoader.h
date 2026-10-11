#pragma once

// .ext 包（zip）的解析与安装，按 regulation.json5 的 Type 分流。
// ClientExtension[Debug] → 客户端扩展：装进 <exe>/clientExtensions/<Name>/，GUI 线程 QPluginLoader 装载；
// 其余 → 工具扩展：服务端扩展目录，Worker 线程按 JSON 协议调用。
//
// （2026-10-01 删）regulation 里的 Subtype 字段与 QQuickSupport 运行时包已退役：宿主自己就是 QML 壳、
// Qt Quick 随宿主部署，不再需要 .ext 形式的运行时包，也不再按 Subtype 做分流/预检。

#include <QString>

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
	};

	bool loadAndInstall(const QString& extFilePath, const QString& serverProfilePath, LoadedExtension* out, QString* error = nullptr);

private:
	bool extractArchive(const QString& extFilePath, const QString& destDir, QString* error);
	bool findFiles(const QString& rootDir, LoadedExtension* out, QString* error);
	bool readDescriptor(const QString& jsonPath, LoadedExtension* out, QString* error);
	bool installPlugin(const QString& pluginPath, const QString& serverProfilePath, LoadedExtension* out, QString* error);

	QString m_errorString;
};
