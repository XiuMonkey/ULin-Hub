#pragma once

// 信号槽登记表：按 index 管理 connect，支持取消 / 挂起 / 重连 / 接管 / 交还。
// QObject 必须是第一个基类（moc 假定首个基类即 QObject 基类）；文末 dshRegister 是宿主内部短接入口。
// 匿名性：插件的元对象里含它的全部信号，任何"插件自定 signal 去 connect"的入口都等于交出宿主整套信号面
// ⇒ RegisterConnection 只在宿主内部用，插件侧新增订阅只走 ProtectedRegisterConnection（按 m_publicSignals 放行）。

#include "VirtualClass/VirtualCommon.h"

#include <QByteArray>
#include <QHash>
#include <QMetaMethod>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <utility>
#include <vector>

class ConnectionManager : public QObject, public VirtualConnectionManager
{
	Q_OBJECT
	Q_INTERFACES(VirtualConnectionManager)

		// 插件被允许新增订阅的信号（QMetaObject 规范化签名）。加条目 = 承诺该信号名稳定。
		std::vector<QByteArray> m_publicSignals = { QByteArrayLiteral("2takeoverChanged(bool)") };

public:
	static ConnectionManager& instance();
	~ConnectionManager() override;

	// ===================== 插件面 =====================
	// Q_INVOKABLE 只标在参数能被 QML 构造的那几个上：QByteArray 在 QML 里映射到 ArrayBuffer，const char* 不在转换表里。
	// HostConnections 是同一个实例的 QML 单例，注入式扩展的 QML 与宿主共用引擎 ⇒ 这里开放的能力扩展也能调。

	Q_INVOKABLE void TakeoverConnection(QString index, QObject* receiver, const QString& slot) { TakeoverConnection(index, receiver, slot.toUtf8()); }  // QML 适配重载

	Q_INVOKABLE void ProtectedRegisterConnection(QString index, const QObject* sender, const QString& signal, const QObject* receiver, const QString& slot)  // QML 适配重载
	{
		const QByteArray sig = signal.toUtf8(), sl = slot.toUtf8();
		ProtectedRegisterConnection(index, sender, sig, receiver, sl.constData());  // constData() 只喂给这次同步调用，不存下来
	}

	void ProtectedRegisterConnection(QString index, const QObject* sender, QByteArray signal, const QObject* receiver, const char* slot) override
	{
		const QByteArray signalName = EnsureSignalPrefix(signal);
		if (std::find(m_publicSignals.begin(), m_publicSignals.end(), signalName) == m_publicSignals.end()) {
			qWarning("ProtectedRegisterConnection(%s): 信号 %s 未公开，已拒绝", qPrintable(index), signalName.constData());
			return;
		}
		const QByteArray slotName = EnsureSlotPrefix(QByteArray(slot));
		RegisterConnection(index, sender, signalName, receiver, slotName.constData());
	}

	Q_INVOKABLE void PublicRemoveConnection(QString index) override
	{
		const auto found = ConnectionRegistry.find(index);
		if (found == ConnectionRegistry.end())
			return;
		if (found.value().Slot == "Lambda") {  // Lambda 槽没名字，只能按 handle 断
			if (found.value().HasHandle)
				QObject::disconnect(found.value().Handle);
		}
		else {
			QObject::disconnect(found.value().Sender, found.value().Signal, found.value().Receiver, found.value().Slot);
		}
		ConnectionRegistry.remove(index);
	}

	Q_INVOKABLE void SuspendConnection(QString index) override  // 挂起：disconnect 但不删表
	{
		const auto found = ConnectionRegistry.find(index);  // 非 const：挂起后要清 HasHandle，让"没有活连接"成为明确状态
		if (found == ConnectionRegistry.end())
			return;  // 未登记的 index 必须早退：对 end() 取 value() 是未定义行为（实测 0xC0000005）
		if (found.value().Slot == "Lambda") {
			if (found.value().HasHandle)
				QObject::disconnect(found.value().Handle);
		}
		else {
			QObject::disconnect(found.value().Sender, found.value().Signal, found.value().Receiver, found.value().Slot);
		}
		found.value().HasHandle = false;
	}

	Q_INVOKABLE void Reconnect(QString index) override
	{
		const auto found = ConnectionRegistry.find(index);
		if (found == ConnectionRegistry.end() || found.value().Slot == "Lambda")
			return;  // 未登记，或 Lambda 槽（没名字，只能由登记方自己重连）
		found.value().Handle = QObject::connect(found.value().Sender, found.value().Signal, found.value().Receiver, found.value().Slot);
		found.value().HasHandle = bool(found.value().Handle);  // 必须存回新 handle，否则"挂起 → 重连 → 再挂起"断的是旧 handle
	}

	void TakeoverConnection(QString index, QObject* receiver, QByteArray slot) override
	{
		const auto found = ConnectionRegistry.find(index);
		if (found == ConnectionRegistry.end()) {
			qWarning("TakeoverConnection(%s): 没有这条连接，忽略", qPrintable(index));  // 原文这里静默，故障看不出来
			return;
		}
		SuspendConnection(index);
		// 留底：把**当前整行**拷一份挂到 Prev 上（链式栈 ⇒ 反复接管可逐层回卷）。Lambda 槽没名字、重连不回来，
		// 不建底。⚠️ 必须在 SuspendConnection 之后、且用 found.value() 拷贝：RegisterConnection 会先删行。
		// 具名赋值：ConnectionGroup 不是聚合初始化的安全目标（按成员位置对位，插字段就整体错位）。
		ConnectionGroup group;
		group.Sender = found.value().Sender;
		group.Receiver = receiver;
		group.Signal = found.value().Signal;
		group.Slot = slot;
		if (found.value().Slot != "Lambda")
			group.Prev = std::make_shared<ConnectionGroup>(found.value());
		const QString taker = takeoverOwnerName(receiver);
		group.TakenOverBy = taker;
		RegisterConnection(index, std::move(group));
		const auto after = ConnectionRegistry.constFind(index);
		if (after != ConnectionRegistry.constEnd() && after.value().HasHandle)
			qInfo("TakeoverConnection(%s): 已改派给 %s（原配方%s）", qPrintable(index), qPrintable(taker),
				  after.value().Prev ? "已留底，可 ReleaseTakeover 交还" : "是 Lambda 槽、留不了底，交还不了");
	}

	Q_INVOKABLE bool ReleaseTakeover(QString index) override
	{
		const auto found = ConnectionRegistry.find(index);
		if (found == ConnectionRegistry.end()) {
			qWarning("ReleaseTakeover(%s): 没有这条连接", qPrintable(index));
			return false;
		}
		if (!found.value().Prev) {
			qWarning("ReleaseTakeover(%s): 接管前的配方留不了底（Lambda 槽），交还失败", qPrintable(index));
			return false;
		}
		// 恢复的是堆上那份的**副本**（副本自带它的 Prev ⇒ 还能继续逐层回卷）。先把要用的取出来：
		// 下面 RegisterConnection 会先 PublicRemoveConnection(index)，那一行连同整条 Prev 链立刻被释放。
		const QString who = found.value().TakenOverBy;
		QObject* const backReceiver = found.value().Prev->Receiver.data();
		const QByteArray backClass = backReceiver ? QByteArray(backReceiver->metaObject()->className()) : QByteArrayLiteral("null");
		ConnectionGroup back = *found.value().Prev;
		RegisterConnection(index, std::move(back));
		qInfo("ReleaseTakeover(%s): %s 交还，原配方已恢复（receiver=%s）", qPrintable(index),
			  qPrintable(who.isEmpty() ? QStringLiteral("接管方") : who), backClass.constData());
		return true;
	}

	int ReleaseTakeoversFor(const QString& owner)  // 卸载扩展时按接管者名整批交还（宿主内部用，不上虚接口）
	{
		if (owner.isEmpty())
			return 0;
		QStringList ids;
		for (auto it = ConnectionRegistry.constBegin(); it != ConnectionRegistry.constEnd(); ++it)
			if (it.value().TakenOverBy == owner) ids.append(it.key());
		int released = 0;
		for (const QString& id : ids) if (ReleaseTakeover(id)) ++released;
		if (released > 0) qInfo("ReleaseTakeoversFor(%s): 交还 %d 条接管", qPrintable(owner), released);
		return released;
	}

	// ===================== 登记 =====================

	// QML 可调：签名由 QML **手写**（前缀 '2'/'1' 不用写）。⚠️ 槽的参数必须带类型标注 —— 未标注的参数在元对象里
	// 是 QVariant，接不上 QString 信号（实测报 Incompatible sender/receiver arguments）。
	// 之所以只能是这样一个非模板重载：moc 不支持函数模板（标 Q_INVOKABLE 也是空操作），且 QML 构造不出 ConnectionGroup。
	Q_INVOKABLE void RegisterConnection(QString index, const QObject* sender, const QString& signal, const QObject* receiver, const QString& slot)
	{
		if (index.isEmpty() || !sender || !receiver || signal.isEmpty() || slot.isEmpty()) {
			qWarning("RegisterConnection(): 有空值，拒绝");
			return;
		}
		ConnectionGroup group;
		group.Sender = const_cast<QObject*>(sender);
		group.Receiver = const_cast<QObject*>(receiver);
		group.Signal = signal.toUtf8();
		group.Slot = slot.toUtf8();
		RegisterConnection(index, std::move(group));  // 不能转给模板：模板把 Slot 记成 "Lambda"，配方就没名字了
	}

	void RegisterConnection(QString index, ConnectionGroup group)  // 宿主内部登记（**不上虚接口**）
	{
		PublicRemoveConnection(index);
		group.Signal = EnsureSignalPrefix(group.Signal);
		group.Slot = EnsureSlotPrefix(group.Slot);
		const auto handle = QObject::connect(group.Sender, group.Signal, group.Receiver, group.Slot);
		if (!handle) {
			// 连不上就别登记：登记了只会让 Suspend / Reconnect 拿空 handle 空转，而调用方拿不到返回值
			qWarning("RegisterConnection(%s): 连接未建立（signal=%s），未登记", qPrintable(index), group.Signal.constData());
			return;
		}
		group.Handle = handle;
		group.HasHandle = true;
		ConnectionRegistry.insert(index, group);
	}

	template <typename Func>
	void RegisterConnection(QString index, const QObject* sender, QByteArray signal, const QObject* receiver, Func slot)
	{
		PublicRemoveConnection(index);
		const auto handle = QObject::connect(sender, signal, receiver, slot);
		if (!handle) {
			qWarning("RegisterConnection(%s): 连接未建立（signal=%s），未登记", qPrintable(index), signal.constData());
			return;
		}
		ConnectionGroup group;  // Lambda / 成员函数指针：槽没有名字，只能按 handle 管，故 Slot 记成 "Lambda"
		group.Sender = const_cast<QObject*>(sender);
		group.Receiver = const_cast<QObject*>(receiver);
		group.Signal = signal;
		group.Slot = "Lambda";
		group.Handle = handle;
		group.HasHandle = true;
		ConnectionRegistry.insert(index, group);
	}

	template <typename Sender, typename Signal, typename Func>
	void RegisterConnection(QString index, const Sender* sender, Signal signal, const QObject* receiver, Func slot)
	{
		PublicRemoveConnection(index);
		const auto handle = QObject::connect(sender, signal, receiver, slot);
		if (!handle) {
			qWarning("RegisterConnection(%s): 连接未建立（成员函数指针形式），未登记", qPrintable(index));
			return;
		}
		ConnectionGroup group;
		group.Sender = const_cast<Sender*>(sender);
		group.Receiver = const_cast<QObject*>(receiver);
		group.Signal = QMetaMethod::fromSignal<Signal>(signal).methodSignature();
		group.Slot = "Lambda";
		group.Handle = handle;
		group.HasHandle = true;
		ConnectionRegistry.insert(index, group);
	}

private:
	// Qt 字符串版 connect 靠首字符认类型：'2' = 信号、'1' = 槽。缺前缀不报错，只打一行警告并返回空连接（静默失效）。
	static QByteArray EnsureSignalPrefix(QByteArray signal);
	static QByteArray EnsureSlotPrefix(QByteArray slot);

	// 接管者记账名：优先取插件的 hostIdentity（安装目录名），卸载时靠同一个名字整批交还；取不到退回类名。
	static QString takeoverOwnerName(QObject* receiver)
	{
		if (!receiver)
			return QStringLiteral("dll");
		const QString who = receiver->property("hostIdentity").toString();
		return who.isEmpty() ? QString::fromLatin1(receiver->metaObject()->className()) : who;
	}

	// （2026-10-10 删）PrivateRemoveConnection 两个重载：从旧版本保留至今、全仓无人调用，
	// 与 PublicRemoveConnection / SuspendConnection 的职责重叠，已整对删除。
	// 退役清单见 docs/retired-interfaces.md。

	QHash<QString, ConnectionGroup> ConnectionRegistry;  // 唯一的数据：配方 + 活连接 handle + 接管前配方 + 接管者
};

template <typename... Args>
inline void dshRegister(const char* index, Args&&... args)
{
	ConnectionManager::instance().RegisterConnection(QString::fromUtf8(index), std::forward<Args>(args)...);
}

template <typename... Args>
inline void dshRegister(const QString& index, Args&&... args)
{
	ConnectionManager::instance().RegisterConnection(index, std::forward<Args>(args)...);
}
