#pragma once

// 扩展 UI 注入宿主接口。由 HostUi 实现（登记名 kUiHost）。

#include <QObject>
#include <QString>
#include <QStringList>
#include <qplugin.h>

class VirtualUiHost
{
public:
	virtual ~VirtualUiHost() = default;

	virtual QObject* HostItem(const char* name) = 0;
	virtual QStringList HostItemNames() = 0;

	// owner = 扩展安装目录名；返回句柄，失败为空串。
	virtual QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) = 0;
	virtual int UnmountOwner(const char* owner) = 0;

	virtual QString OpenWindow(const char* owner, const char* qmlUrl, QObject* context, const char* title) = 0;
	virtual int CloseWindow(const char* owner) = 0;
	virtual QString OpenWebWindow(const char* owner, const char* url, const char* title) = 0;

	// 账户入口接管：扩展接管后宿主不再打开内置账户页。
	virtual bool ClaimAccountEntry(const char* owner) { Q_UNUSED(owner); return false; }
	virtual void ReleaseAccountEntry(const char* owner) { Q_UNUSED(owner); }
};
Q_DECLARE_INTERFACE(VirtualUiHost, "com.ulinhub.VirtualUiHost/1.0")
