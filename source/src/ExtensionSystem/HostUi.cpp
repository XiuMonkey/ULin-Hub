#include "ExtensionSystem/HostUi.h"

#include <QDebug>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QUrl>
#include <QVariantMap>

HostUi& HostUi::instance()
{
	static HostUi ui;
	return ui;
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
		// 扩展注入靠 setParentItem，所以宿主对象必须是 Item；不是就明确拒绝，别让它静默失效
		qWarning("[HostUi] registerItem(%s): 不是 QQuickItem，拒绝", qPrintable(name));
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
	// 顺序：先删注入项（它可能引用 context），再删 context
	if (entry.item) {
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

bool HostUi::SetPanelBackend(const QString& owner, const QString& kind, QObject* backend)
{
	if (kind.isEmpty())
		return false;
	if (!backend) {                  // 空后端 = 撤销，省得调用方自己判
		ClearPanelBackend(owner, kind);
		return true;
	}

	PanelBackend& entry = m_panelBackends[kind];
	entry.owner = owner;             // 按 kind 只记一份：后登记者取得该 kind，前一份被顶掉
	entry.backend = backend;
	qInfo().noquote() << QStringLiteral("[HostUi] %1: %2 块的后端改由扩展提供（%3）")
		.arg(owner, kind, QString::fromLatin1(backend->metaObject()->className()));
	return true;
}

void HostUi::ClearPanelBackend(const QString& owner, const QString& kind)
{
	const auto found = m_panelBackends.constFind(kind);
	// 只撤自己登记的：别的扩展顶上来之后，旧扩展的卸载不该把新登记清掉
	if (found == m_panelBackends.constEnd() || found.value().owner != owner)
		return;

	m_panelBackends.remove(kind);
}

bool HostUi::HasPanelBackend(const QString& kind)
{
	return m_panelBackends.constFind(kind) != m_panelBackends.constEnd();
}

QObject* HostUi::panelBackend(const QString& kind) const
{
	const auto found = m_panelBackends.constFind(kind);
	return found == m_panelBackends.constEnd() ? nullptr : found.value().backend.data();
}

int HostUi::releasePanelBackends(const QString& owner)
{
	int removed = 0;
	for (auto it = m_panelBackends.begin(); it != m_panelBackends.end();) {
		if (it.value().owner == owner) {
			it = m_panelBackends.erase(it);
			++removed;
		}
		else {
			++it;
		}
	}
	return removed;
}
