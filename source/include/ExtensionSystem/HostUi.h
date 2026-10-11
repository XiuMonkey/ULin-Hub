#pragma once

// 扩展 UI 宿主（QML 单例 `HostUi` / 插件侧 index `kUiHost`）
//
// 职责只有三件（不做槽位、不做贡献清单、不做 mount 编排 —— 见 VirtualCommon.h 的 VirtualUiHost）：
//   ① 宿主把可注入的界面对象按**稳定名字**登记出来：QML 根里一行
//        Component.onCompleted: HostUi.registerItem("topBar", this)
//   ② 扩展"拿到对象直接注入"：
//        DLL → DshHost::findObject("topBar") / VirtualUiHost::MountInto(...)
//        QML → HostUi.item("topBar")（然后在自己的 QML 里写 parent: … / anchors）
//   ③ 记账 owner → 注入项，卸载时整批摘掉（UnmountOwner / 退出时 unmountAll）
//
// 名字契约（稳定，加名字要跟版本一起发）：shell / titleBar / topBar / sidebar / composer
//   · topBar 的根是 Row（位置器）⇒ 注入进去**自动排在末尾**（= 旧控件壳 layout->addWidget 的手感）
//   · 其余是自由 Item ⇒ 位置由扩展自己的 QML 写 anchors / x,y

#include "VirtualClass/VirtualCommon.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class QQmlContext;
class QQuickItem;

class HostUi : public QObject, public VirtualUiHost
{
	Q_OBJECT
		Q_INTERFACES(VirtualUiHost)
		Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)

public:
	static HostUi& instance();

	// ---------- VirtualUiHost（DLL 插件面）----------
	QObject* HostItem(const char* name) override;
	QStringList HostItemNames() override;
	QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) override;
	int UnmountOwner(const char* owner) override;

	// ---------- QML 面 ----------
	QStringList names() const;
	// 宿主 QML 根自己登记（重复登记同一个对象是幂等的）
	Q_INVOKABLE bool registerItem(const QString& name, QObject* item);
	Q_INVOKABLE QObject* item(const QString& name) const;
	// url 必须是绝对路径或 file:/qrc: URL（扩展自己知道自己在哪；宿主不猜）
	Q_INVOKABLE QString mountInto(const QString& owner, const QString& hostItem, const QString& url, QObject* context = nullptr);
	Q_INVOKABLE int unmountOwner(const QString& owner);
	Q_INVOKABLE int unmountAll();
	// 诊断：当前挂了哪些（id / owner / 宿主 / 对象名）
	Q_INVOKABLE QVariantList mounted() const;

signals:
	void namesChanged();

private:
	struct Mounted
	{
		QString id;
		QString owner;
		QString hostName;
		QPointer<QQuickItem> item;
		QPointer<QQmlContext> context;
	};

	QString mountIntoImpl(const QString& owner, const QString& hostItem, const QString& url, QObject* context);
	void dropAt(int index);

	QHash<QString, QPointer<QObject>> m_items;   // 名字 → 宿主对象（QML item）
	QVector<Mounted> m_mounted;                  // 注入项记账
	int m_nextId = 1;
};
