#pragma once
// QML 暴露面：组合 common 服务，不继承、不持有 Hub。
#include "qmlbridge/BridgeCommon.h"

// 样式逻辑路径 → 实际 URL，转发给 StyleResolver。
class StyleBridge : public QObject
{
	Q_OBJECT

public:
	static StyleBridge& instance();

	Q_INVOKABLE QString url(const QString& logicalPath) const
	{
		return StyleResolver::instance().url(logicalPath);
	}
	Q_INVOKABLE bool reset() { return StyleResolver::instance().reset(); }
	Q_INVOKABLE void reload() { StyleResolver::instance().reload(); }

private:
	explicit StyleBridge(QObject* parent = nullptr) : QObject(parent) {}
};

// 按索引登记 QML 侧的 connect()，统一交给 ConnectionManager 管理生命周期。
class HostConnectionsBridge : public QObject
{
	Q_OBJECT

public:
	static HostConnectionsBridge& instance();

	Q_INVOKABLE void RegisterConnection(QString index, const QObject* sender, const QString& signal, const QObject* receiver, const QString& slot)
	{
		ConnectionManager::instance().RegisterConnection(index, sender, signal, receiver, slot);
	}

private:
	explicit HostConnectionsBridge(QObject* parent = nullptr) : QObject(parent) {}
};

// 扩展可用的宿主 UI 入口：账号入口占用、UI 项登记/挂载、块后端释放。
class HostUiBridge : public QObject
{
	Q_OBJECT

public:
	static HostUiBridge& instance();

	Q_INVOKABLE bool claimAccountEntry(const QString& owner)
	{
		return HostUi::instance().claimAccountEntry(owner);
	}
	Q_INVOKABLE void releaseAccountEntry(const QString& owner)
	{
		HostUi::instance().releaseAccountEntry(owner);
	}
	Q_INVOKABLE bool registerItem(const QString& name, QObject* item)
	{
		return HostUi::instance().registerItem(name, item);
	}
	Q_INVOKABLE QObject* item(const QString& name) const
	{
		return HostUi::instance().item(name);
	}
	Q_INVOKABLE QString mountInto(const QString& owner, const QString& hostItem, const QString& url, QObject* context = nullptr)
	{
		return HostUi::instance().mountInto(owner, hostItem, url, context);
	}
	Q_INVOKABLE int unmountOwner(const QString& owner)
	{
		return HostUi::instance().unmountOwner(owner);
	}
	Q_INVOKABLE int unmountAll() { return HostUi::instance().unmountAll(); }
	Q_INVOKABLE QVariantList mounted() const { return HostUi::instance().mounted(); }
	Q_INVOKABLE QObject* blockBackend(const QString& kind) const
	{
		return HostUi::instance().blockBackend(kind);
	}
	Q_INVOKABLE int releaseBlockBackends(const QString& owner)
	{
		return HostUi::instance().releaseBlockBackends(owner);
	}

private:
	explicit HostUiBridge(QObject* parent = nullptr) : QObject(parent) {}
};