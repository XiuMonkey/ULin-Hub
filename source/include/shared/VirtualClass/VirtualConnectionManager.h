#pragma once

// 信号槽登记表宿主接口。由 ConnectionManager 实现（登记名 kConnectionManager）。

#include <QByteArray>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <qplugin.h>

#include <memory>

class VirtualConnectionManager
{
public:
	struct ConnectionGroup
	{
		QPointer<QObject> Sender;
		QPointer<QObject> Receiver;
		QByteArray Signal;
		QByteArray Slot;
		QMetaObject::Connection Handle;
		bool HasHandle = false;
		QString TakenOverBy;
		std::shared_ptr<ConnectionGroup> Prev;
	};

	virtual ~VirtualConnectionManager() = default;

	// 新增白名单订阅；signal 是原生签名串（如 "2clearRequested"）。
	virtual void ProtectedRegisterConnection(QString index, const QObject* sender, QByteArray signal, const QObject* receiver, const char* slot) = 0;

	// 接管宿主公开信号；index 见连接表。
	virtual void TakeoverConnection(QString index, QObject* receiver, QByteArray slot) = 0;

	// 交还接管，恢复接管前配方。
	virtual bool ReleaseTakeover(QString index) = 0;

	virtual void SuspendConnection(QString index) = 0;
	virtual void PublicRemoveConnection(QString index) = 0;
	virtual void Reconnect(QString index) = 0;
};
Q_DECLARE_INTERFACE(VirtualConnectionManager, "com.ulinhub.VirtualConnectionManager/1.0")
