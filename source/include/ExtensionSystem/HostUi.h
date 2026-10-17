#pragma once

// 扩展 UI 宿主（QML 单例 `HostUi` / 插件侧 index `kUiHost`）
//
// 职责（不做槽位、不做贡献清单、不做 mount 编排 —— 见 VirtualCommon.h 的 VirtualUiHost / VirtualPanelHost）：
//   ① 宿主把可注入的界面对象按**稳定名字**登记出来：QML 根里一行
//        Component.onCompleted: HostUi.registerItem("topBar", this)
//   ② 扩展"拿到对象直接注入"：
//        DLL → DshHost::findObject("topBar") / VirtualUiHost::MountInto(...)
//        QML → HostUi.item("topBar")（然后在自己的 QML 里写 parent: … / anchors）
//   ③ 记账 owner → 注入项，卸载时整批摘掉（UnmountOwner / 退出时 unmountAll）
//   ④ 顶替（VirtualPanelHost）：扩展登记某个块（当前只有 "terminal"）的**后端**改由它的对象担任，
//      界面仍是宿主的 —— 终端块照旧由 TerminalPanel 渲染，只是页面接上的对象换了。按 kind 只记一份，
//      卸载时 releasePanelBackends 撤。
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

class HostUi : public QObject, public VirtualUiHost, public VirtualPanelHost
{
	Q_OBJECT
		Q_INTERFACES(VirtualUiHost VirtualPanelHost)
		Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)

public:
	static HostUi& instance();

	// ---------- VirtualUiHost（DLL 插件面）----------
	QObject* HostItem(const char* name) override;
	QStringList HostItemNames() override;
	QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) override;
	int UnmountOwner(const char* owner) override;

	// ---------- VirtualPanelHost（DLL 插件面）----------
	bool SetPanelBackend(const QString& owner, const QString& kind, QObject* backend) override;
	void ClearPanelBackend(const QString& owner, const QString& kind) override;
	bool HasPanelBackend(const QString& kind) override;

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

	// 顶替的后端（见 VirtualPanelHost）：TerminalPanel 在建 QWebChannel 时取一次；空 = 用内置桥。
	Q_INVOKABLE QObject* panelBackend(const QString& kind) const;
	// 卸载扩展时整批撤（ClientExtension::remove 那条路上调，与 ReleaseTakeoversFor 并列）
	Q_INVOKABLE int releasePanelBackends(const QString& owner);

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

	// 顶替某个块的后端（见 VirtualPanelHost）；QPointer 弱引用：后端对象先死则自动置空，面板读到空就回内置
	struct PanelBackend
	{
		QString owner;
		QPointer<QObject> backend;
	};

	QString mountIntoImpl(const QString& owner, const QString& hostItem, const QString& url, QObject* context);
	void dropAt(int index);

	QHash<QString, QPointer<QObject>> m_items;   // 名字 → 宿主对象（QML item）
	QVector<Mounted> m_mounted;                  // 注入项记账
	int m_nextId = 1;
	QHash<QString, PanelBackend> m_panelBackends;   // kind → 顶替者（按 kind 只记一份）
};
