#pragma once

// 分层（领域归位 2026-10-15）：本文件在 common/shell/ —— **领域与纯逻辑层**（块列表的状态机本体：
// 增删、同类编号、活动索引收敛、"关光了回主界面"都在这里）。它不 import 任何 QML 概念
// （只用 QtCore 的 QAbstractListModel），可以脱离 QML 引擎单测（见 misc/tools/cpp-selfcheck 的 ②）。
// QML 面（单例名 `Dock`、类型注册）在 src/core/main.cpp 与 qml/basic/shell/PanelDock.qml，
// 那边只接线、不放算法。
//
// 面板坞（右栏顶部那些"块" = 页签）的模型 —— 从 basic/shell/PanelDock.qml 的 JS 数组搬来。
//
// 为什么进 C++（与仓库既有分层一致）：块的增删、同类编号、活动索引收敛、"关光了回主界面"
// 都是能被字符串/数字独立描述、脱离窗口就能断言的东西。留在 QML 里既没法单测，
// 也会随风格包被拷一份出来（PanelDock.qml 是可被风格包覆盖的单元）。
//
// 内容路由按同一条判据归位（2026-10-15 二改）：`kind → 视图` 的映射表在 viewFor()，与中文块名
// kindLabel() 同层 —— QML 只按 C++ 给的那组值装机（url / 要不要当前块 / 兜底文案），
// 路由表达式里不再出现任何 kind 字符串。搬不走的只有"把 url 变成实例"这一步（那是 QML 语义）。
//
// 行为与搬家前**逐条对齐** —— 下面几条都是实测出来的规矩，别顺手"优化"：
//   · 块按打开先后追加；标题在**开块那一刻**按"同类已有几块"编号（"网页浏览 1"/"网页浏览 2"），
//     关块**不重排**已有标题（块的身份不该因为别人被关掉就变）；
//   · open() 追加 + 激活 + 离开主界面；
//   · close() 后活动索引**只在越界时**收敛（`activeIndex >= count ⇒ count-1`）：关掉活动块
//     前面那块时索引不动 ⇒ 选中的是"原来的下一块"，这也是既有行为；
//   · showHome()（⌂）只切显示，块全部保留；
//   · 通知语义与 QML 属性一致：值没变就不发通知（QML 赋同值不会发 changed，PanelDock 的
//     内容重放动画正是挂在两个属性的 changed 上 —— 多发一次就会多播一次动画）。
//
// 进程内一份（右栏只有一份面板坞，块列表要跨"右栏开合 / 风格包重载"活着），
// 因此做成 QML 单例（QML 名 `Dock`，注册见 main.cpp）。

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVector>

class PanelDockModel : public QAbstractListModel
{
	Q_OBJECT
		Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
		Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)
		Q_PROPERTY(bool homeVisible READ homeVisible NOTIFY homeVisibleChanged)
		// 当前块：QML 的内容路由（Loader.sourceComponent）就认这三个 + 下面那组视图值。
		// ⚠️ 不能让 QML 自己写 `dock.at(activeIndex)` 建绑定：JS 函数调用是**一次性求值**，
		//    QML 追踪不到"模型行变了"，关掉活动块前面那块时路由会停在旧块上（内容与页签不一致）。
		Q_PROPERTY(QString activeKind READ activeKind NOTIFY activeChanged)
		Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY activeChanged)
		// 活动块的**稳定 id**（块一旦开出来就不再变；索引会随关块前移，id 不会）。
		// 终端块用它当会话 key（Terminal.session(blockId)）：索引当 key 会在关掉前面那块时串台。
		Q_PROPERTY(QString activeBlockId READ activeBlockId NOTIFY activeChanged)
		// 当前块的内容视图 = `kind → 视图` 的映射结果（viewFor），QML 只按这组值装机：
		//   · activeViewPath        视图的逻辑路径（QML 侧过 Style.url()，<exe>/ui/ 同名文件优先）；
		//                           空 = 这个块没有视图，显示 activeViewDetail 的文案
		//   · activeViewBlockScoped 视图要拿到当前块的稳定 id（终端：切同类块时宿主不重建 item，
		//                           靠它把新块推给面板）
		//   · activeViewDetail      没有视图时的说明（未接入 / 缺运行时依赖）
		Q_PROPERTY(QString activeViewPath READ activeViewPath NOTIFY activeViewChanged)
		Q_PROPERTY(bool activeViewBlockScoped READ activeViewBlockScoped NOTIFY activeViewChanged)
		Q_PROPERTY(QString activeViewDetail READ activeViewDetail NOTIFY activeViewChanged)
	// 启动器目录（三种块的中文名与说明只有这一份，QML 不再拼）
	Q_PROPERTY(QVariantList launcherItems READ launcherItems CONSTANT)
	// ---- 脱离成独立窗口（面板坞的"块脱离"）----
	// 状态放模型、窗口对象放 QML（qmlbridge 只接线、common 只放领域）：这里只管"哪个块脱离了 /
	// 独立窗口是否还显示 / 独立窗口的几何"，QML 侧 Dock.detachedBlocks 驱动 one-window-per-block 的
	// DetachedBlockWindow；窗口的 move/resize 再回写下面那组几何 ⇒ 几何在"关掉独立窗口再打开"、
	// 甚至风格包重载后都还在。
	Q_PROPERTY(QVariantList detachedBlocks READ detachedBlocks NOTIFY detachedChanged)
	// 有没有"已脱离且独立窗口还显示"的块 —— 顶栏 ◫ 的三态状态机就靠它
	// （true ⇒ 这一次点击是"关闭那个独立窗口"，块回到"已附着但隐藏"）
	Q_PROPERTY(bool anyDetachedVisible READ anyDetachedVisible NOTIFY detachedChanged)

public:
	enum Role
	{
		KindRole = Qt::UserRole + 1,   // "terminal" | "browser" | "files"
		TitleRole,
		BlockIdRole                    // 稳定 id（"blk-1"…）；见 activeBlockId
	};

	// 视图表的一行：逻辑路径（空 = 没有视图，看 detail）+ 要不要当前块 + 兜底文案
	struct ViewSpec
	{
		QString path;
		bool blockScoped = false;
		QString detail;
	};

	static PanelDockModel& instance();   // 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎

	// kind 的中文名；未知 kind 与搬家前一样兜底成"文件浏览"
	static QString kindLabel(const QString& kind);

	// `kind → 视图` 的映射表。webEngineAvailable 参与解析：缺 WebEngine 时浏览器/终端降级成说明页。
	static ViewSpec viewFor(const QString& kind, bool webEngineAvailable);

	int activeIndex() const { return m_activeIndex; }
	bool homeVisible() const { return m_homeVisible; }
	QString activeKind() const;
	QString activeTitle() const;
	QString activeBlockId() const;
	QString activeViewPath() const;
	bool activeViewBlockScoped() const;
	QString activeViewDetail() const;
	QVariantList launcherItems() const;
	// 已脱离的块（每项：blockId / kind / title / viewPath / viewBlockScoped / viewDetail / visible +
	// 独立窗口几何 x/y/w/h）。QML 侧只读它，增删与几何一律走下面的方法。
	QVariantList detachedBlocks() const;
	bool anyDetachedVisible() const;

	// 宿主能力：本构建带不带 Qt WebEngine（vcxproj 的 webenginequick + main.cpp 的
	// QtWebEngineQuick::initialize()，缺一都会在运行期崩而不是报错）。由 main.cpp 启动时注入。
	// 默认 false 是安全侧：忘了注入只会看到说明页，不会崩。
	bool webEngineAvailable() const { return m_webEngineAvailable; }
	void setWebEngineAvailable(bool available);

	// 开一个块：追加 + 激活 + 离开主界面
	Q_INVOKABLE void open(const QString& kind);
	// 关第 index 个块（越界忽略）；关光了回主界面，活动索引收敛到有效值
	Q_INVOKABLE void close(int index);
	// 切到第 index 个块（越界忽略；切换同时离开主界面）
	Q_INVOKABLE void activate(int index);
	// ⌂ 主界面：只切显示，块保留
	Q_INVOKABLE void showHome();

	// ---- 脱离 / 回附（块的独立窗口）----
	// ⚠️ 这几个都用**稳定 blockId**做 key，不用行号：独立窗口是长命的，而块表会随关块前移行号
	//    （关掉一个块就换一个行号指到别人身上 —— 那会让"把 A 拖回来"变成"把 B 拖回来"）。
	// 把第 index 个块脱离成独立窗口：记状态；几何由 QML 算好默认值再回写
	// （所以"拖回去再脱离"会回到上次的位置与尺寸，而不是跳回正中）。越界忽略。
	Q_INVOKABLE void detach(int index);
	// 把 id 对应的块附着回坞里：清脱离状态（独立窗口随 detachedBlocks 少一项而销毁），
	// 并把它激活 + 打开右栏 —— 也就是"回到它原来待的地方"。
	Q_INVOKABLE void attach(const QString& blockId);
	// 独立窗口显示 / 关闭（关闭 = 块回到"已附着但隐藏"：脱离状态与几何都留着，
	// 顶栏 ◫ 的下一次点击默认以**附着**状态打开）
	Q_INVOKABLE void setDetachedVisible(const QString& blockId, bool visible);
	// 关闭**所有**可见的独立窗口（顶栏 ◫ 在"已独立"那一态做的事）；返回关掉了几个
	Q_INVOKABLE int closeDetachedWindows();
	// 独立窗口的几何回写（窗口 move/resize 时由 QML 调；模型不自己动窗口）
	Q_INVOKABLE void setDetachedGeometry(const QString& blockId, int x, int y, int w, int h);

	// ---- 抓手的"脱离"接线（QML 侧的广播口，不是模型状态）----
	// 为什么放在这个单例上：三个点抓手从 PanelDock.qml 的页签条搬到了 RightSidebar.qml 的标题行，
	// 而"落点预览开关"与"请求把某个块脱离出去"原本是 **PanelDock.qml 内部的信号** —— 搬过去就够不着了。
	// 这两条都属于壳层级接线（提示由 Shell.qml 画、独立窗口由 Shell.qml 建），放模型单例上正是
	// "任意面板都能发起脱离"的那个广播口；模型**不因此存任何状态**（预览开关是纯 UI 状态，
	// 仍归画提示的壳层：Shell.dropHintActive）。
	//
	// 请求把 blockId 这个块脱离成独立窗口：atScreenX/Y 是**屏幕坐标**（跨窗口判断只能用屏幕坐标，
	// 各自窗口的场景原点不是一个）。Shell.qml 接住它；块不存在/已经是脱离态时由壳层忽略。
	Q_INVOKABLE void requestDetach(const QString& blockId, qreal atScreenX, qreal atScreenY);
	// 拖动过程中的落点预览开关（拖动期间主窗口收不到 hover，只能由正在拖的抓手推过来）
	Q_INVOKABLE void setDetachPreview(bool overTarget);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

signals:
	void countChanged();
	void activeIndexChanged();
	void homeVisibleChanged();
	void activeChanged();
	void activeViewChanged();
	void detachedChanged();   // 脱离集合 / 独立窗口可见性 / 独立窗口几何（三者共用一条，QML 侧整表重读）
	// 抓手的脱离请求（见 requestDetach）：**广播**，模型自己不处理 —— 接住它的是壳层
	// （Shell.qml 建独立窗口 / 亮落点提示）。刻意不做成"模型里存一个待处理请求"：
	// 那就把 QML 的窗口语义漏进 common/** 了（本文件的分层说明）。
	void detachRequested(const QString& blockId, qreal atScreenX, qreal atScreenY);
	void detachPreviewChanged(bool overTarget);

private:
	// 一块的"独立窗口"状态。挂在 Block 上而不是单独一张表：块被关掉时状态跟着一起没了
	// （不用另写清理逻辑，也不会留下指向已删块的悬空项）。
	struct Detached
	{
		bool active = false;          // 已脱离（窗口对象由 QML 按 detachedBlocks 建）
		bool visible = false;         // 独立窗口当前显示中（false = "已附着但隐藏"）
		int x = 0;                    // 独立窗口几何。QML 的 Window.x/y/width/height 是逻辑像素，
		int y = 0;                    //   这里原样存 QML 给的数，不换算
		int w = 0;                    // 0 = 还没定过，QML 首次显示时给默认值再回写
		int h = 0;
	};

	struct Block
	{
		QString kind;
		QString title;
		QString id;    // 稳定 id（见 activeBlockId）；只增不重用
		Detached detached;   // 独立窗口状态（见 Detached）
	};

	explicit PanelDockModel(QObject* parent = nullptr);

	void setActiveIndex(int index);
	void setHomeVisible(bool visible);
	// 按块 id 找行（-1 = 没有）；脱离那几条都用 id 做 key，不用索引（索引会随关块前移）
	int rowForId(const QString& id) const;

	QVector<Block> m_blocks;
	int m_activeIndex = -1;              // -1 = 没有活动块（与搬家前 `activeIndex: -1` 同义）
	bool m_homeVisible = true;           // 开局是主界面（启动器）
	bool m_webEngineAvailable = false;   // 宿主能力，见 setWebEngineAvailable
	int m_nextBlockSeq = 1;              // 稳定 id 的自增号
};
