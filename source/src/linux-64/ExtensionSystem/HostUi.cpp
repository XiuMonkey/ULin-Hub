#include "ExtensionSystem/HostUi.h"

#include "common/shell/PanelBarDockModel.h"

#include <QDebug>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QWindow>
#include <QQuickWindow>
#include <QQuickItem>
#include <QUrl>
#include <QVariantMap>

// 进程内单例：QML 侧以 `HostUi` 单例名访问，DLL 侧由 kUiHost 取到同一个实例，
// 两边共用同一份登记表与注入记账。
HostUi& HostUi::instance()
{
	static HostUi ui;
	return ui;
}

bool HostUi::ClaimAccountEntry(const char* owner)
{
	return claimAccountEntry(QString::fromUtf8(owner ? owner : ""));
}

void HostUi::ReleaseAccountEntry(const char* owner)
{
	releaseAccountEntry(QString::fromUtf8(owner ? owner : ""));
}

// 账户入口接管（accountEntryOwner）：非空即表示宿主不再开内置账户页，改由该扩展自己开窗。
// 后登记的 owner 直接覆盖前一个；空名不生效。
bool HostUi::claimAccountEntry(const QString& owner)
{
	const QString clean = owner.trimmed();
	if (clean.isEmpty())
		return false;
	m_accountEntryOwner = clean;
	emit accountEntryOwnerChanged();
	qInfo().noquote() << QStringLiteral("[HostUi] 账户入口已交给扩展：%1").arg(clean);
	return true;
}

// 只释放自己那份接管：owner 不匹配说明入口已被别的扩展顶替，保留当前接管者。
void HostUi::releaseAccountEntry(const QString& owner)
{
	if (m_accountEntryOwner != owner)
		return;
	m_accountEntryOwner.clear();
	emit accountEntryOwnerChanged();
}

// 已登记宿主对象名的排序副本：诊断与日志用，顺序与登记先后无关。
QStringList HostUi::names() const
{
	QStringList out = m_items.keys();
	out.sort();
	return out;
}

// VirtualUiHost 的 C ABI 版本：DLL 侧按 UTF-8 名字枚举宿主对象清单。
QStringList HostUi::HostItemNames()
{
	return names();
}

// C ABI 版本：按 UTF-8 名字取宿主对象，找不到返回 nullptr。
QObject* HostUi::HostItem(const char* name)
{
	if (!name || !*name)
		return nullptr;
	return m_items.value(QString::fromUtf8(name)).data();
}

// 宿主 QML 根在 Component.onCompleted 里登记自己；名字是稳定契约（见 HostUi.h 的名字清单）。
// 重复登记同一个对象算成功，方便 QML 重载时再登记一次。
bool HostUi::registerItem(const QString& name, QObject* item)
{
	if (name.isEmpty() || !item) {
		qWarning("[HostUi] registerItem: 名字或对象为空，忽略");
		return false;
	}
	if (!qobject_cast<QQuickItem*>(item)) {
		qWarning("[HostUi] registerItem(%s): 不是 QQuickItem，拒绝", qPrintable(name));   // 扩展注入靠 setParentItem，所以宿主对象必须是 Item；不是就明确拒绝，别让它静默失效
		return false;
	}
	if (m_items.value(name).data() == item)
		return true;   // 幂等
	m_items.insert(name, item);
	qInfo().noquote() << QStringLiteral("[HostUi] 可挂对象已登记:") << name;
	emit namesChanged();
	return true;
}

QObject* HostUi::item(const QString& name) const
{
	return m_items.value(name).data();
}

QString HostUi::MountInto(const char* owner, const char* hostItem, const char* qmlUrl, QObject* context)
{
	return mountIntoImpl(QString::fromUtf8(owner ? owner : ""), QString::fromUtf8(hostItem ? hostItem : ""), QString::fromUtf8(qmlUrl ? qmlUrl : ""), context);
}

QString HostUi::mountInto(const QString& owner, const QString& hostItem, const QString& url, QObject* context)
{
	return mountIntoImpl(owner, hostItem, url, context);
}

// 注入实现：查宿主对象 → 校验 URL → 建组件 → 建子上下文 → 实例化 → setParentItem → 记账。
// 任一步失败都回收已建对象并返回空 id；成功返回记账 id（owner#序号）。
QString HostUi::mountIntoImpl(const QString& owner, const QString& hostItem, const QString& url, QObject* context)
{
	if (owner.isEmpty() || hostItem.isEmpty() || url.isEmpty()) {
		qWarning("[HostUi] mountInto: owner / 宿主名 / url 有空值，拒绝");
		return QString();
	}

	auto* host = qobject_cast<QQuickItem*>(m_items.value(hostItem).data()); // 必须是 Item：注入靠 setParentItem
	if (!host) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): 宿主对象 %2 不存在（没登记或名字拼错；现有：%3）").arg(owner, hostItem, names().join(QLatin1Char(',')));
		return QString();
	}

	QQmlEngine* engine = qmlEngine(host);
	if (!engine) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): 宿主对象没有 QML 引擎").arg(owner);
		return QString();
	}

	// 只接受绝对路径 / file: / qrc: —— 扩展自己知道自己在哪，宿主不去猜相对路径
	const QUrl source = (url.startsWith(QLatin1String("file:")) || url.startsWith(QLatin1String("qrc:")))
		? QUrl(url)
		: QUrl::fromLocalFile(QFileInfo(url).absoluteFilePath());

	auto* component = new QQmlComponent(engine, source, this); // 组件挂宿主：实例化完即可 deleteLater
	if (component->isError()) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): 载入失败 %2\n%3")
			.arg(owner, source.toString(), component->errorString());
		component->deleteLater();
		return QString();
	}

	// context 非空时才注入：扩展的 QML 根写 `required property var context` 就必须给（DLL 场景）
	auto* qmlContext = new QQmlContext(engine->rootContext(), this);
	QVariantMap initial;
	if (context)
		initial.insert(QStringLiteral("context"), QVariant::fromValue(context));
	QObject* created = initial.isEmpty() ? component->create(qmlContext) // 没有 required 属性就走普通 create
										 : component->createWithInitialProperties(initial, qmlContext);
	component->deleteLater(); // 组件只用于实例化，创建完即可回收

	auto* injected = qobject_cast<QQuickItem*>(created);
	if (!injected) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): %2 的根不是 Item%s")
			.arg(owner, source.fileName(),
				 created ? QStringLiteral("（是 %1）").arg(QString::fromLatin1(created->metaObject()->className()))
						 : QStringLiteral("（创建失败：%1）").arg(component->errorString()));
		if (created)
			created->deleteLater();
		delete qmlContext;
		return QString();
	}

	injected->setParentItem(host); // 注入点：位置/排序交给扩展 QML 自己处理（位置器里自动排末尾）
	if (!injected->parentItem()) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): setParentItem(%2) 失败").arg(owner, hostItem);
		injected->deleteLater();
		delete qmlContext;
		return QString();
	}

	Mounted entry; // id 只用于诊断/日志；卸载按 owner 整批摘，不依赖 id
	entry.id = QStringLiteral("%1#%2").arg(owner).arg(m_nextId++);
	entry.owner = owner;
	entry.hostName = hostItem;
	entry.item = injected;
	entry.context = qmlContext;
	m_mounted.append(entry);

	qInfo().noquote() << QStringLiteral("[HostUi] %1: 已把 %2 注入到 %3（id=%4）")
		.arg(owner, source.fileName(), hostItem, entry.id);
	return entry.id;
}

int HostUi::UnmountOwner(const char* owner)
{
	return owner ? unmountOwner(QString::fromUtf8(owner)) : 0;
}

// 从记账里移除并销毁一个注入项；item/context 都可能是空（对象已被 QML 先销毁）。
void HostUi::dropAt(int index)
{
	const Mounted entry = m_mounted.at(index);
	m_mounted.removeAt(index);
	if (entry.item) {   // 顺序：先删注入项（它可能引用 context），再删 context
		entry.item->setParentItem(nullptr);
		delete entry.item;
	}
	if (entry.context)
		delete entry.context;
}

// 按 owner 整批摘：扩展卸载/退出时调用，其它 owner 的注入不受影响。
int HostUi::unmountOwner(const QString& owner)
{
	int removed = 0;
	// 倒序遍历：dropAt 用 removeAt 收缩数组，正序会跳过相邻项
	for (int i = m_mounted.size() - 1; i >= 0; --i) {
		if (m_mounted.at(i).owner != owner)
			continue;
		dropAt(i);
		++removed;
	}
	if (removed > 0)
		qInfo().noquote() << QStringLiteral("[HostUi] %1: 摘下 %2 个注入项").arg(owner).arg(removed);
	return removed;
}

// 退出清理：全量摘下注入项，避免 QML 引擎析构顺序造成悬空引用。
int HostUi::unmountAll()
{
	const int total = m_mounted.size();
	while (!m_mounted.isEmpty())
		dropAt(m_mounted.size() - 1);
	if (total > 0)
		qInfo().noquote() << QStringLiteral("[HostUi] 退出清理：摘下 %1 个注入项").arg(total);
	return total;
}

// 诊断快照：每项给出 id / owner / 宿主名 / 对象名，供调试面板直接展示。
QVariantList HostUi::mounted() const
{
	QVariantList rows;
	for (const Mounted& entry : m_mounted) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), entry.id);
		row.insert(QStringLiteral("owner"), entry.owner);
		row.insert(QStringLiteral("host"), entry.hostName);
		row.insert(QStringLiteral("objectName"), entry.item ? entry.item->objectName() : QString());
		rows.append(row);
	}
	return rows;
}

// 顶替某个块的后端（kind 如 "terminal"）：空 backend 等于撤销；按 kind 只留一份，后登记者顶掉前一个。
bool HostUi::SetBlockBackend(const QString& owner, const QString& kind, QObject* backend)
{
	if (kind.isEmpty())
		return false;
	if (!backend) {                  // 空后端 = 撤销，省得调用方自己判
		ClearBlockBackend(owner, kind);
		return true;
	}

	BlockBackend& entry = m_blockBackends[kind];
	entry.owner = owner;             // 按 kind 只记一份：后登记者取得该 kind，前一份被顶掉
	entry.backend = backend;
	qInfo().noquote() << QStringLiteral("[HostUi] %1: %2 块的后端改由扩展提供（%3）")
		.arg(owner, kind, QString::fromLatin1(backend->metaObject()->className()));
	emit blockBackendChanged(kind); // 通知已建好的面板重新取后端
	return true;
}

void HostUi::ClearBlockBackend(const QString& owner, const QString& kind)
{
	const auto found = m_blockBackends.constFind(kind);
	if (found == m_blockBackends.constEnd() || found.value().owner != owner)   // 只撤自己登记的：别的扩展顶上来之后，旧扩展的卸载不该把新登记清掉
		return;

	m_blockBackends.remove(kind);
	emit blockBackendChanged(kind);
}

// 面板建 QWebChannel 前先问有没有扩展顶替的后端。
bool HostUi::HasBlockBackend(const QString& kind)
{
	return m_blockBackends.constFind(kind) != m_blockBackends.constEnd();
}

// 取顶替后端：QPointer 弱引用，后端对象先死则自动置空，面板读到空就回内置桥。
QObject* HostUi::blockBackend(const QString& kind) const
{
	const auto found = m_blockBackends.constFind(kind);
	return found == m_blockBackends.constEnd() ? nullptr : found.value().backend.data();
}

// 卸载扩展时整批撤：一个扩展可能顶替多个 kind，按 owner 扫描。
int HostUi::releaseBlockBackends(const QString& owner)
{
	int removed = 0;
	for (auto it = m_blockBackends.begin(); it != m_blockBackends.end();) {
		if (it.value().owner == owner) {
			it = m_blockBackends.erase(it);
			++removed;
		}
		else {
			++it;
		}
	}

	return removed;
}

// 以下三个是扩展侧入口的薄转发：真实状态在 PanelBarDockModel，这里不改语义。
// launcher = 面板栏上的入口按钮（kind 唯一，label/hint 是显示文案），由扩展注册、卸载时反注册。
void HostUi::RegisterLauncher(const char* kind, const char* label, const char* hint)
{
	PanelBarDockModel::instance().registerLauncher(QString::fromUtf8(kind ? kind : ""), QString::fromUtf8(label ? label : ""), QString::fromUtf8(hint ? hint : ""));
}

void HostUi::UnregisterLauncher(const char* kind)
{
	PanelBarDockModel::instance().unregisterLauncher(QString::fromUtf8(kind ? kind : ""));
}

void HostUi::OpenBlock(const char* kind)
{
	PanelBarDockModel::instance().open(QString::fromUtf8(kind ? kind : "")); // 打开已注册的块（拉起面板）
}

// 把扩展给的产物路径交给输入框：不存在的路径直接丢弃，返回实际转交的数量。
int HostUi::AttachFilesToComposer(const char* const* paths, int count)
{
	if (!paths || count <= 0)
		return 0;
	QStringList list;
	for (int i = 0; i < count; ++i) {
		const QString path = QString::fromUtf8(paths[i] ? paths[i] : "");
		if (!path.isEmpty() && QFileInfo::exists(path))
			list.append(path);
	}
	if (list.isEmpty())
		return 0;
	qInfo().noquote() << QStringLiteral("[HostUi] AttachFilesToComposer -> %1 个文件").arg(list.size());
	emit composerFilesRequested(list); // 宿主 QML 侧转给 ChatController 的待发附件列表
	return list.size();
}

QString HostUi::OpenBlockUrl(const QString& url, const QString& title)
{
	// 返回空 blockId 表示被模型拒绝（url 为空），调用方不用再判空
	const QString blockId = PanelBarDockModel::instance().openUrl(url, title);
	if (blockId.isEmpty()) {
		qWarning().noquote() << QStringLiteral("[HostUi] OpenBlockUrl 拒绝：url 为空");
		return blockId;
	}
	qInfo().noquote() << QStringLiteral("[HostUi] OpenBlockUrl -> %1（块 %2）").arg(url, blockId);
	return blockId;
}

bool HostUi::CloseBlock(const QString& blockId)
{
	const bool closed = PanelBarDockModel::instance().closeBlock(blockId); // false = 块不存在或已关闭
	if (closed)
		qInfo().noquote() << QStringLiteral("[HostUi] CloseBlock: 已关块 %1").arg(blockId);
	return closed;
}

// 借一个 QML 引擎：宿主对象都挂在同一个引擎上，扩展窗口/组件必须建在同一引擎里。
QQmlEngine* HostUi::anyEngine() const
{
	for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
		if (QObject* item = it.value().data()) {
			if (QQmlEngine* engine = qmlEngine(item))
				return engine;
		}
	}
	return nullptr;
}

// 主窗口：从任一已登记 item 的 window() 取；用于给扩展窗口设 transient parent 与联动关闭。
QWindow* HostUi::mainWindow() const
{
	for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
		if (auto* item = qobject_cast<QQuickItem*>(it.value().data())) {
			if (QWindow* window = item->window())
				return window;
		}
	}
	return nullptr;
}

// 开窗实现：借引擎 → 同一 owner 先关旧窗 → 建组件/子上下文 → 实例化 QQuickWindow → 记账 →
// 挂到主窗口（transient parent + 主窗关闭联动）→ 显示并返回记账 id。
QString HostUi::openWindowImpl(const QString& owner, const QUrl& source, const QVariantMap& initialProps, const QString& title)
{
	if (owner.isEmpty() || source.isEmpty()) {
		qWarning("[HostUi] openWindow: owner / source 有空值，拒绝");
		return QString();
	}

	QQmlEngine* engine = anyEngine();
	if (!engine) {
		qWarning() << QStringLiteral("[HostUi] openWindow(%1): 还没有可用的 QML 引擎（宿主对象还没登记？）").arg(owner);
		return QString();
	}

	CloseWindow(owner.toUtf8().constData());   // 同一 owner 只留一个窗口

	auto* component = new QQmlComponent(engine, source, this); // 组件归宿主：载入失败就 deleteLater 丢掉
	if (component->isError()) {
		qWarning().noquote() << QStringLiteral("[HostUi] openWindow(%1): 载入失败 %2\n%3")
			.arg(owner, source.fileName(), component->errorString());
		component->deleteLater();
		return QString();
	}

	auto* windowContext = new QQmlContext(engine->rootContext(), this); // 每个窗口一个子上下文：context 只对本次窗口可见
	if (initialProps.contains(QStringLiteral("context")))
		windowContext->setContextProperty(QStringLiteral("context"), initialProps.value(QStringLiteral("context")));

	QObject* created = initialProps.isEmpty() ? component->create(windowContext)
	                                          : component->createWithInitialProperties(initialProps, windowContext);
	auto* window = qobject_cast<QQuickWindow*>(created); // 根必须是 Window/ApplicationWindow，否则按失败回收
	if (!window) {
		qWarning().noquote() << QStringLiteral("[HostUi] openWindow(%1): %2 载入/构造失败：%3")
			.arg(owner, source.fileName(), component->errorString());
		delete created;
		windowContext->deleteLater();
		component->deleteLater();
		return QString();
	}

	if (!title.isEmpty())
		window->setTitle(title);

	WindowEntry entry;
	entry.id = QStringLiteral("%1#window%2").arg(owner).arg(m_nextId++);
	entry.owner = owner;
	entry.window = window;
	entry.context = windowContext;
	m_windows.insert(owner, entry);

	if (QWindow* main = mainWindow()) {   // 挂到主窗口上：① 始终浮在主窗口之上 ② 主窗口一关，这些窗口一起关
		window->setTransientParent(main);
		if (!m_mainHooked) {
			m_mainHooked = true;
			QObject::connect(main, &QWindow::visibleChanged, this, [this](bool visible) {
				if (visible)
					return;
				for (const WindowEntry& entry : qAsConst(m_windows)) {
					if (auto* w = qobject_cast<QWindow*>(entry.window.data()))
						w->close();
				}
			});
		}
	}

	QObject::connect(window, &QWindow::visibleChanged, this, [this, owner, window](bool visible) {   // 用户点 × / 代码 close 都会走到这里：回收上下文与窗口对象
		if (visible)
			return;
		auto it = m_windows.find(owner);
		if (it != m_windows.end() && it->window == window) {
			if (it->context)
				it->context->deleteLater();
			m_windows.erase(it);
		}
		window->deleteLater();
		qInfo().noquote() << QStringLiteral("[HostUi] 窗口已关闭：%1").arg(owner);
	});

	window->show();
	window->raise(); // 三步组合：确保扩展窗口一出现就在最前并获得焦点
	window->requestActivate();
	qInfo().noquote() << QStringLiteral("[HostUi] %1: 已开窗口 %2（%3）")
		.arg(owner, entry.id, source.fileName());
	return entry.id;
}

// C ABI 版本（VirtualUiHost）：context 非空时作为初始属性注入，满足 QML 根上的 required property。
QString HostUi::OpenWindow(const char* ownerC, const char* urlC, QObject* context, const char* titleC)
{
	const QString owner = QString::fromUtf8(ownerC ? ownerC : "");
	const QString url = QString::fromUtf8(urlC ? urlC : "");
	QVariantMap initial;
	if (context)
		initial.insert(QStringLiteral("context"), QVariant::fromValue(context));
	return openWindowImpl(owner, QUrl(url), initial, QString::fromUtf8(titleC ? titleC : ""));
}

// 打开宿主自带的 WebEngine 窗口（qrc 里的 WebWindow.qml）：扩展只给 URL 与标题。
QString HostUi::OpenWebWindow(const char* ownerC, const char* urlC, const char* titleC)
{
	const QString owner = QString::fromUtf8(ownerC ? ownerC : "");
	const QString page = QString::fromUtf8(urlC ? urlC : "");
	if (owner.isEmpty() || page.isEmpty()) {
		qWarning("[HostUi] openWebWindow: owner / url 有空值，拒绝");
		return QString();
	}
	QVariantMap initial;   // 通用兜底落点：宿主自带的 WebEngine 窗口去解析这个 URL（宿主不认识任何插件）
	initial.insert(QStringLiteral("initialUrl"), page);
	return openWindowImpl(owner, QUrl(QStringLiteral("qrc:/qml/basic/browser/WebWindow.qml")),
		initial, QString::fromUtf8(titleC ? titleC : ""));
}

// 关窗：只发 close 请求，窗口对象与上下文的回收统一在 visibleChanged(false) 里做。
int HostUi::CloseWindow(const char* ownerC)
{
	const QString owner = QString::fromUtf8(ownerC ? ownerC : "");
	auto it = m_windows.find(owner);
	if (it == m_windows.end() || !it->window)
		return 0;
	auto* window = qobject_cast<QWindow*>(it->window.data());
	if (!window)
		return 0;
	window->close();   // visibleChanged(false) 负责回收
	return 1;
}
