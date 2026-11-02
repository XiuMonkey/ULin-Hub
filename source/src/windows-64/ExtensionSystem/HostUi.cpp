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

void HostUi::releaseAccountEntry(const QString& owner)
{
	if (m_accountEntryOwner != owner)
		return;
	m_accountEntryOwner.clear();
	emit accountEntryOwnerChanged();
}

QStringList HostUi::names() const
{
	QStringList out = m_items.keys();
	out.sort();
	return out;
}

QStringList HostUi::HostItemNames()
{
	return names();
}

QObject* HostUi::HostItem(const char* name)
{
	if (!name || !*name)
		return nullptr;
	return m_items.value(QString::fromUtf8(name)).data();
}

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
	return mountIntoImpl(QString::fromUtf8(owner ? owner : ""),
						 QString::fromUtf8(hostItem ? hostItem : ""),
						 QString::fromUtf8(qmlUrl ? qmlUrl : ""), context);
}

QString HostUi::mountInto(const QString& owner, const QString& hostItem, const QString& url, QObject* context)
{
	return mountIntoImpl(owner, hostItem, url, context);
}

QString HostUi::mountIntoImpl(const QString& owner, const QString& hostItem, const QString& url, QObject* context)
{
	if (owner.isEmpty() || hostItem.isEmpty() || url.isEmpty()) {
		qWarning("[HostUi] mountInto: owner / 宿主名 / url 有空值，拒绝");
		return QString();
	}

	auto* host = qobject_cast<QQuickItem*>(m_items.value(hostItem).data());
	if (!host) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): 宿主对象 %2 不存在（没登记或名字拼错；"
											  "现有：%3）").arg(owner, hostItem, names().join(QLatin1Char(',')));
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

	auto* component = new QQmlComponent(engine, source, this);
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
	QObject* created = initial.isEmpty() ? component->create(qmlContext)
										 : component->createWithInitialProperties(initial, qmlContext);
	component->deleteLater();

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

	injected->setParentItem(host);
	if (!injected->parentItem()) {
		qWarning().noquote() << QStringLiteral("[HostUi] mountInto(%1): setParentItem(%2) 失败").arg(owner, hostItem);
		injected->deleteLater();
		delete qmlContext;
		return QString();
	}

	Mounted entry;
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

int HostUi::unmountOwner(const QString& owner)
{
	int removed = 0;
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

int HostUi::unmountAll()
{
	const int total = m_mounted.size();
	while (!m_mounted.isEmpty())
		dropAt(m_mounted.size() - 1);
	if (total > 0)
		qInfo().noquote() << QStringLiteral("[HostUi] 退出清理：摘下 %1 个注入项").arg(total);
	return total;
}

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
	emit blockBackendChanged(kind);
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

bool HostUi::HasBlockBackend(const QString& kind)
{
	return m_blockBackends.constFind(kind) != m_blockBackends.constEnd();
}

QObject* HostUi::blockBackend(const QString& kind) const
{
	const auto found = m_blockBackends.constFind(kind);
	return found == m_blockBackends.constEnd() ? nullptr : found.value().backend.data();
}

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

void HostUi::RegisterLauncher(const char* kind, const char* label, const char* hint)
{
	PanelBarDockModel::instance().registerLauncher(
		QString::fromUtf8(kind ? kind : ""),
		QString::fromUtf8(label ? label : ""),
		QString::fromUtf8(hint ? hint : ""));
}

void HostUi::UnregisterLauncher(const char* kind)
{
	PanelBarDockModel::instance().unregisterLauncher(QString::fromUtf8(kind ? kind : ""));
}

void HostUi::OpenBlock(const char* kind)
{
	PanelBarDockModel::instance().open(QString::fromUtf8(kind ? kind : ""));
}

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
	emit composerFilesRequested(list);
	return list.size();
}

QString HostUi::OpenBlockUrl(const QString& url, const QString& title)
{
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
	const bool closed = PanelBarDockModel::instance().closeBlock(blockId);
	if (closed)
		qInfo().noquote() << QStringLiteral("[HostUi] CloseBlock: 已关块 %1").arg(blockId);
	return closed;
}

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

	auto* component = new QQmlComponent(engine, source, this);
	if (component->isError()) {
		qWarning().noquote() << QStringLiteral("[HostUi] openWindow(%1): 载入失败 %2\n%3")
			.arg(owner, source.fileName(), component->errorString());
		component->deleteLater();
		return QString();
	}

	auto* windowContext = new QQmlContext(engine->rootContext(), this);
	if (initialProps.contains(QStringLiteral("context")))
		windowContext->setContextProperty(QStringLiteral("context"), initialProps.value(QStringLiteral("context")));

	QObject* created = initialProps.isEmpty() ? component->create(windowContext)
	                                          : component->createWithInitialProperties(initialProps, windowContext);
	auto* window = qobject_cast<QQuickWindow*>(created);
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
	window->raise();
	window->requestActivate();
	qInfo().noquote() << QStringLiteral("[HostUi] %1: 已开窗口 %2（%3）")
		.arg(owner, entry.id, source.fileName());
	return entry.id;
}

QString HostUi::OpenWindow(const char* ownerC, const char* urlC, QObject* context, const char* titleC)
{
	const QString owner = QString::fromUtf8(ownerC ? ownerC : "");
	const QString url = QString::fromUtf8(urlC ? urlC : "");
	QVariantMap initial;
	if (context)
		initial.insert(QStringLiteral("context"), QVariant::fromValue(context));
	return openWindowImpl(owner, QUrl(url), initial, QString::fromUtf8(titleC ? titleC : ""));
}

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
