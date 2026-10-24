#pragma once

// 扩展 UI 宿主（QML 单例 `HostUi` / 插件侧 index `kUiHost`）
// 职责（不做槽位、不做贡献清单、不做 mount 编排 —— 见 VirtualCommon.h 的 VirtualUiHost / VirtualBlockBackendHost）：
//  ① 宿主把可注入的界面对象按**稳定名字**登记出来：QML 根里一行
//  Component.onCompleted: HostUi.registerItem("topBar", this)
//  ② 扩展"拿到对象直接注入"：
//  DLL → DshHost::findObject("topBar") / VirtualUiHost::MountInto(...)
//  QML → HostUi.item("topBar")（然后在自己的 QML 里写 parent: … / anchors）
//  ③ 记账 owner → 注入项，卸载时整批摘掉（UnmountOwner / 退出时 unmountAll）
//  ④ 顶替（VirtualBlockBackendHost）：扩展登记某个块（当前只有 "terminal"）的**后端**改由它的对象担任，
//  界面仍是宿主的 —— 终端块照旧由 TerminalPanel 渲染，只是页面接上的对象换了。按 kind 只记一份，
//  卸载时 releaseBlockBackends 撤。
// 名字契约（稳定，加名字要跟版本一起发）：shell / titleBar / topBar / sessionBar / sessionBar.extDock /
//  panelBarDock.extDock / composer
// · topBar 的根是 Row（位置器）⇒ 注入进去**自动排在末尾**
//  · sessionBar.extDock（左栏底部图标行）与 panelBarDock.extDock（右栏标题行）都是 Row（位置器）
//  ⇒ 扩展的入口按钮自动排在末尾；"功能面"类入口用右栏那个，会话级才用左栏那个
//  · 其余是自由 Item ⇒ 位置由扩展自己的 QML 写 anchors / x,y

#include "VirtualClass/VirtualCommon.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QQmlContext;
class QQmlEngine;
class QWindow;
class QQuickItem;

class HostUi : public QObject, public VirtualUiHost, public VirtualBlockBackendHost
{
	Q_OBJECT
		Q_INTERFACES(VirtualUiHost VirtualBlockBackendHost)
		Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)

public:
	static HostUi& instance();

	QObject* HostItem(const char* name) override;
	QStringList HostItemNames() override;
	QString MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context) override;
	int UnmountOwner(const char* owner) override;
	QString OpenWindow(const char* owner, const char* qmlUrl, QObject* context, const char* title) override;
	int CloseWindow(const char* owner) override;
	QString OpenWebWindow(const char* owner, const char* url, const char* title) override;

	bool SetBlockBackend(const QString& owner, const QString& kind, QObject* backend) override;
	void ClearBlockBackend(const QString& owner, const QString& kind) override;
	bool HasBlockBackend(const QString& kind) override;
	// 虚接口层（末尾追加）：**只做转发** —— 底下 PanelBarDockModel 的 openUrl / closeBlock 是模型本来
	// 就有的方法，为扩展没改过一行。扩展 include 宿主头、拿到本单例后 cast 调它们。
	QString OpenBlockUrl(const QString& url, const QString& title) override;
	void RegisterLauncher(const char* kind, const char* label, const char* hint) override;
	void UnregisterLauncher(const char* kind) override;
	int AttachFilesToComposer(const char* const* paths, int count) override;
	void OpenBlock(const char* kind) override;
	bool CloseBlock(const QString& blockId) override;

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

	// 顶替的后端（见 VirtualBlockBackendHost）：TerminalPanel 在建 QWebChannel 时取一次；空 = 用内置桥。
	Q_INVOKABLE QObject* blockBackend(const QString& kind) const;
	// 卸载扩展时整批撤（ClientExtension::remove 那条路上调，与 ReleaseTakeoversFor 并列）
	Q_INVOKABLE int releaseBlockBackends(const QString& owner);

signals:
	// 扩展把产物交给输入框：QmlBridge 收到后转给 ChatController 的待发附件列表
	void composerFilesRequested(const QStringList& paths);
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

	// 顶替某个块的后端（见 VirtualBlockBackendHost）；QPointer 弱引用：后端对象先死则自动置空，面板读到空就回内置
	struct BlockBackend
	{
		QString owner;
		QPointer<QObject> backend;
	};

	QString mountIntoImpl(const QString& owner, const QString& hostItem, const QString& url, QObject* context);
	// 开窗口的共用实现：initialProps 用来满足 QML 根上的 required property
	QString openWindowImpl(const QString& owner, const QUrl& source, const QVariantMap& initialProps, const QString& title);

	// 顶层窗口记账（OpenWindow / CloseWindow）：QPointer 弱引用，窗口先死就自动置空
	struct WindowEntry
	{
		QString id;
		QString owner;
		QPointer<QObject> window;
		QPointer<QQmlContext> context;
	};
	QQmlEngine* anyEngine() const;   // 从已登记的宿主对象借一个 QML 引擎（窗口要在它上面建组件）
	QWindow* mainWindow() const;     // 宿主主窗口（从已登记 item 的 window() 取；取不到就 nullptr）
	void dropAt(int index);

	QHash<QString, QPointer<QObject>> m_items;   // 名字 → 宿主对象（QML item）
	QVector<Mounted> m_mounted;                  // 注入项记账
	int m_nextId = 1;
	QHash<QString, BlockBackend> m_blockBackends;   // kind → 顶替者（按 kind 只记一份）
	QHash<QString, WindowEntry> m_windows;           // owner → 顶层窗口
	bool m_mainHooked = false;                       // 主窗口关闭 → 连带关掉扩展窗口（只连一次）
};
