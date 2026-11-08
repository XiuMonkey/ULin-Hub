#pragma once

// 面板块后端替换宿主接口。由 HostUi 实现（登记名 kUiHost，与 VirtualUiHost 同一个单例）。

#include <QObject>
#include <QString>
#include <qplugin.h>

class VirtualBlockBackendHost
{
public:
	virtual ~VirtualBlockBackendHost() = default;

	virtual bool SetBlockBackend(const QString& owner, const QString& kind, QObject* backend) = 0;
	virtual void ClearBlockBackend(const QString& owner, const QString& kind) = 0;
	virtual bool HasBlockBackend(const QString& kind) = 0;

	virtual void OpenBlock(const char* kind) = 0;
	virtual QString OpenBlockUrl(const QString& url, const QString& title) = 0;
	virtual bool CloseBlock(const QString& blockId) = 0;

	virtual void RegisterLauncher(const char* kind, const char* label, const char* hint) = 0;
	virtual void UnregisterLauncher(const char* kind) = 0;

	// 把本地文件塞进输入框；返回入队个数。
	virtual int AttachFilesToComposer(const char* const* paths, int count) = 0;
};
Q_DECLARE_INTERFACE(VirtualBlockBackendHost, "com.ulinhub.VirtualBlockBackendHost/1.0")
