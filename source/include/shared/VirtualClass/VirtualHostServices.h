#pragma once

// 宿主业务对象的扩展面接口。
// 统一约定：
//  · 只暴露“扩展确实需要”的读状态 / 动作，不把整个 QObject 的 Q_INVOKABLE 面交出去；
//  · 方法顺序固定为：状态读取 → 动作 → 页面/生命周期，新增方法只追加到最后一组；
//  · 每个实现只 allowlist 一个真实函数，不在虚函数里写业务分支；
//  · 扩展调用虚接口，QML 仍走 Q_INVOKABLE/Hub 属性；两套入口不互相替代。

#include <QObject>
#include <QString>
#include <QVariantList>

class VirtualChatHost
{
public:
	virtual ~VirtualChatHost() = default;

	virtual QString ChatSessionId() const = 0;
	virtual bool ChatStreaming() const = 0;
	virtual int ChatMessageCount() const = 0;

	virtual void ChatSetSession(const QString& sessionId) = 0;
	virtual void ChatCancel() = 0;
	virtual void ChatRefreshModelCatalog() = 0;
	virtual void ChatSelectModel(const QString& provider, const QString& model) = 0;
	virtual void ChatSelectLevel(const QString& levelId) = 0;
	virtual void ChatAddQuote(const QString& text) = 0;
};
Q_DECLARE_INTERFACE(VirtualChatHost, "com.ulinhub.VirtualChatHost/1.0")

class VirtualSettingsHost
{
public:
	virtual ~VirtualSettingsHost() = default;

	virtual QString SettingsMasterBackend() const = 0;
	virtual QString SettingsServerUrl() const = 0;
	virtual bool SettingsOpen() const = 0;
	virtual int SettingsPage() const = 0;

	virtual void SettingsRefresh() = 0;
	virtual void SettingsRefreshMasterBackends() = 0;
	virtual void SettingsOpenPage() = 0;
	virtual void SettingsClosePage() = 0;
	virtual void SettingsSetPage(int page) = 0;
};
Q_DECLARE_INTERFACE(VirtualSettingsHost, "com.ulinhub.VirtualSettingsHost/1.0")

class VirtualCommandHost
{
public:
	virtual ~VirtualCommandHost() = default;

	virtual bool CommandAvailable() const = 0;
	virtual QVariantList CommandItems() const = 0;

	virtual void CommandRefresh(const QString& sessionId) = 0;
	virtual void CommandExecute(const QString& sessionId, const QString& line) = 0;
};
Q_DECLARE_INTERFACE(VirtualCommandHost, "com.ulinhub.VirtualCommandHost/1.0")

class VirtualFileBrowserHost
{
public:
	virtual ~VirtualFileBrowserHost() = default;

	virtual QString FileBrowserRootPath() const = 0;
	virtual QString FileBrowserCurrentPath() const = 0;

	virtual void FileBrowserOpenRoot(const QString& path) = 0;
	virtual void FileBrowserNavigate(const QString& path) = 0;
	virtual void FileBrowserOpenFile(const QString& path) = 0;
	virtual void FileBrowserRefresh() = 0;
	virtual void FileBrowserCloseFile() = 0;
};
Q_DECLARE_INTERFACE(VirtualFileBrowserHost, "com.ulinhub.VirtualFileBrowserHost/1.0")

class VirtualPluginMarketHost
{
public:
	virtual ~VirtualPluginMarketHost() = default;

	virtual bool PluginMarketInstalling() const = 0;
	virtual int PluginMarketActiveTaskCount() const = 0;

	virtual void PluginMarketOpenPage() = 0;
	virtual void PluginMarketClosePage() = 0;
	virtual void PluginMarketRefresh() = 0;
	virtual void PluginMarketInstall(const QString& url) = 0;
	virtual void PluginMarketUpdate(const QString& name) = 0;
	virtual void PluginMarketUninstall(const QString& name) = 0;
};
Q_DECLARE_INTERFACE(VirtualPluginMarketHost, "com.ulinhub.VirtualPluginMarketHost/1.0")

class VirtualExtensionHost
{
public:
	virtual ~VirtualExtensionHost() = default;

	virtual bool ExtensionOpen() const = 0;
	virtual QVariantList ExtensionItems() const = 0;
	virtual bool ExtensionCanRemove() const = 0;
	virtual QString ExtensionRestartHint() const = 0;

	virtual void ExtensionOpenPage() = 0;
	virtual void ExtensionClosePage() = 0;
	virtual void ExtensionRefresh() = 0;
	virtual void ExtensionInstallFromFile() = 0;
	virtual bool ExtensionRemoveSelected() = 0;
	virtual void ExtensionCleanupResiduals() = 0;
	virtual void ExtensionRestartServer() = 0;
};
Q_DECLARE_INTERFACE(VirtualExtensionHost, "com.ulinhub.VirtualExtensionHost/1.0")

