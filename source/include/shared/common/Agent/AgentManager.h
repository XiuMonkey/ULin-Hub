#pragma once

// Agent 生命周期管理器：只管理“已加载扩展里的 Master 后端实例”，
// 不负责扩展 DLL 的安装/卸载。切换时调用成对的
//   Deactivate(old) -> Activate(new)
// 而不是插件对象的析构/构造。
//
// 扩展通过 VirtualApiHost::RegisterMasterAgent(owner, this) 注册；
// 提供 VirtualMasterAgent（id/name/description + Activate/Deactivate）。
// 通讯层只消费 activeSink() 和 activeId()。

#include "VirtualClass/VirtualApiTakeover.h"
#include "VirtualClass/VirtualMasterAgent.h"

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVariantList>

class AgentManager : public QObject
{
	Q_OBJECT

public:
	explicit AgentManager(QObject* parent = nullptr);

	// owner = 扩展安装目录名；同一 id 重复注册以最后一次为准。
	bool registerAgent(const QString& owner, QObject* agent);
	void unregisterOwner(const QString& owner);

	QVariantList registeredAgents() const;
	QString activeId() const { return m_activeId; }
	VirtualApiSink* activeSink() const;

	// 用户选择的后端（持久化在 AgentSetting.json）；与运行期 activeId 分开。
	QString selectedId() const;
	void setSelectedId(const QString& backendId);

	// 切换后端：先 Deactivate(old)，再 Activate(new)。id=="dsh" 只停用。
	bool activate(const QString& backendId);
	void deactivate();
	void clear();

signals:
	void registeredChanged();
	void activeChanged(const QString& backendId);

private:
	struct Entry
	{
		QString owner;
		QString id;
		QString name;
		QString description;
		QPointer<QObject> object;
	};
	const Entry* find(const QString& id) const;
	void emitRegistered();

	QHash<QString, Entry> m_entries;
	QString m_activeId;
};
