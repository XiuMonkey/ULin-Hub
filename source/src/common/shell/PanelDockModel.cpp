// 见 PanelDockModel.h：右栏面板坞的块（页签）列表。行为逐条对齐搬家前的 JS 数组。

#include "common/shell/PanelDockModel.h"

#include <QModelIndex>
#include <QVariantMap>

namespace
{
	const char kKindTerminal[] = "terminal";
	const char kKindBrowser[] = "browser";

	// 启动器一行（{ kind, label, hint }）；label 统一由 kindLabel() 给，中文名只有一份。
	QVariantMap launcherRow(const QString& kind, const QString& hint)
	{
		QVariantMap item;
		item.insert(QStringLiteral("kind"), kind);
		item.insert(QStringLiteral("label"), PanelDockModel::kindLabel(kind));
		item.insert(QStringLiteral("hint"), hint);
		return item;
	}
}

PanelDockModel& PanelDockModel::instance()
{
	static PanelDockModel model;
	return model;
}

PanelDockModel::PanelDockModel(QObject* parent)
	: QAbstractListModel(parent)
{
}

QString PanelDockModel::kindLabel(const QString& kind)
{
	if (kind == QLatin1String(kKindBrowser))
		return QStringLiteral("网页浏览");
	if (kind == QLatin1String(kKindTerminal))
		return QStringLiteral("终端");
	return QStringLiteral("文件浏览");   // 与搬家前一致：未知 kind 兜底成"文件浏览"
}

QString PanelDockModel::activeKind() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).kind;
}

QString PanelDockModel::activeTitle() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).title;
}

QString PanelDockModel::activeBlockId() const
{
	if (m_activeIndex < 0 || m_activeIndex >= m_blocks.size())
		return QString();
	return m_blocks.at(m_activeIndex).id;
}

QVariantList PanelDockModel::launcherItems() const
{
	// 与搬家前 PanelDock.qml 里那张表逐字一致（顺序、文案都没动）
	QVariantList items;
	items.append(launcherRow(QStringLiteral("terminal"),
		QStringLiteral("在当前会话的工作目录里跑命令（引擎的 terminal 面）")));
	items.append(launcherRow(QStringLiteral("browser"),
		QStringLiteral("用 Qt WebEngine 在右栏看网页")));
	items.append(launcherRow(QStringLiteral("files"),
		QStringLiteral("浏览工作区文件（待接入 workspaceFiles）")));
	return items;
}

void PanelDockModel::open(const QString& kind)
{
	// 同类计数：标题在**开块这一刻**定下来（关块不重排，见头文件）
	int seq = 0;
	for (const Block& block : m_blocks) {
		if (block.kind == kind)
			++seq;
	}

	const int row = m_blocks.size();
	beginInsertRows(QModelIndex(), row, row);
	m_blocks.append(Block{ kind, kindLabel(kind) + QLatin1Char(' ') + QString::number(seq + 1),
		QStringLiteral("blk-%1").arg(m_nextBlockSeq++) });
	endInsertRows();
	emit countChanged();

	setActiveIndex(row);      // 追加即激活（新索引必然不同于旧值 ⇒ 一定会通知）
	setHomeVisible(false);    // 并且离开主界面
}

void PanelDockModel::close(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	beginRemoveRows(QModelIndex(), index, index);
	m_blocks.removeAt(index);
	endRemoveRows();
	emit countChanged();

	// 活动索引只在越界时收敛（搬家前的 `if (activeIndex >= list.length) activeIndex = list.length - 1`）。
	// 关掉活动块**前面**那块时索引不动 ⇒ 活动块换成"原来的下一块"，这是既有行为。
	if (m_activeIndex >= m_blocks.size())
		setActiveIndex(m_blocks.size() - 1);
	else
		emit activeChanged();   // 索引没动，但行少了 ⇒ 活动块可能就是另一块（关的是它前面那块）

	if (m_blocks.isEmpty())
		setHomeVisible(true);   // 关光了自动回主界面
}

void PanelDockModel::activate(int index)
{
	if (index < 0 || index >= m_blocks.size())
		return;

	setActiveIndex(index);    // 点当前块是空操作：值没变 ⇒ 不重放内容动画（与搬家前一致）
	setHomeVisible(false);
}

void PanelDockModel::showHome()
{
	setHomeVisible(true);
}

void PanelDockModel::setActiveIndex(int index)
{
	if (m_activeIndex == index)
		return;

	m_activeIndex = index;
	emit activeIndexChanged();
	emit activeChanged();
}

void PanelDockModel::setHomeVisible(bool visible)
{
	if (m_homeVisible == visible)
		return;

	m_homeVisible = visible;
	emit homeVisibleChanged();
}

int PanelDockModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : m_blocks.size();
}

QVariant PanelDockModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
		return QVariant();

	const Block& block = m_blocks.at(index.row());
	switch (role) {
	case KindRole:
		return block.kind;
	case TitleRole:
		return block.title;
	case BlockIdRole:
		return block.id;
	default:
		return QVariant();
	}
}

QHash<int, QByteArray> PanelDockModel::roleNames() const
{
	QHash<int, QByteArray> names;
	names.insert(KindRole, "kind");
	names.insert(TitleRole, "title");
	names.insert(BlockIdRole, "blockId");
	return names;
}
