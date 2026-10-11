#pragma once

// 样式资源 + 生效的唯一入口（程序级 QObject 单例）。
//
// M8 起：**对象是 QML 风格包**（原职责不变 —— 以前释放/管理各板块 *.qss，现在释放/管理 qrc:/qml 整棵树
// 与 theme-*.json）。颜色来自 theme-<mode>.json，尺寸/圆角/字号来自 metrics.json（QML 侧读）；
// QSS 合成与"把样式表挂到窗口"已随旧控件壳删除，相关入口保留为无操作桩（插件可能仍调）。
// 释放规则不变：内建在 qrc，外部 stylesDir 缺失的文件启动时释放一份供定制（**已存在的绝不覆盖**）。
// 所有方法只在 GUI 线程调用。

#include "VirtualClass/VirtualCommon.h"

#include <QDebug>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QWidget;

class ThemeManager : public QObject, public VirtualTheme
{
	Q_OBJECT
	Q_INTERFACES(VirtualTheme)

public:
	enum class Mode { Light, Dark };

	// 程序级唯一实例；刻意不析构：注册表必须活得比所有被登记对象更久
	static ThemeManager& instance();

	~ThemeManager() override;
	// 启动期（QApplication 之后、界面之前）调用：释放风格包 → 读调色板 → 登记进注册表
	void init(const QString& stylesDir, Mode mode);

	void setMode(Mode mode);

	// 重读风格包目录里的调色板并安装 QPalette（QML 侧 token 热重载由 ThemeBridge 负责）
	void reload();

	// ---- QML 风格包（M8）----
	// 风格包目录（= <exe>/styles）
	QString stylePackDir() const { return m_stylesDir; }
	// 把内置 QML 风格包（qrc:/qml 整棵树 + theme-light/dark.json）**按文件补缺**释放到 stylePackDir()：
	// 缺哪个补哪个，已存在的一个字都不动（用户改过的不能被覆盖），并给回读写权限
	void ensureStylePack();
	// 重置风格包：删掉"内置包里有的"已释放文件（用户自建的不动）→ 从内置重放 → reload()
	void resetStylePack();

	// (M8) QSS 已删除：这两个入口保留为无操作桩（VirtualTheme 接口不能改，插件可能仍调）
	void applyToWindow(QWidget* window);
	void repolishScrollArea(QWidget* widget);

	// VirtualTheme 接口：只转发到上面的入口，插件与宿主必须走同一条路，别加额外语义
	// 插件侧只能 qobject_cast<VirtualTheme*>（跨模块 dynamic_cast 返回 nullptr）；接口只能增不能改
	void ExternalApplyToWindow(QWidget* window) override
	{
		applyToWindow(window);
	}

	bool ExternalReloadStyles() override
	{
		// 未 init 时没有可信的风格包目录
		if (m_stylesDir.isEmpty()) {
			qWarning().noquote() << "[Theme] ExternalReloadStyles before init, refused";
			return false;
		}

		reload();  // 插件先改 <exe>/styles 下的 QML/JSON，再调这里重载调色板（QML 重载需重启界面）

		// 一份调色板都读不到 = 风格包不可用，如实回给插件
		if (m_palette.isEmpty()) {
			qWarning().noquote() << "[Theme] ExternalReloadStyles: 调色板为空，风格包可能已损坏:"
				<< m_stylesDir;
			return false;
		}
		return true;
	}

	// VirtualTheme 末尾追加的两条：查询当前是否深色、按"用户显式选主题"那条路切换
	bool ExternalIsDarkMode() override
	{
		return isDark();
	}

	void ExternalSwitchTheme(QWidget* currentWindow) override
	{
		// 与侧栏主题按钮同一条路；M8 起 switchTheme 只切模式（QML 壳不重建窗口）
		switchTheme(currentWindow);
	}

	bool isDark() const;

	// 代码只引用语义 key，颜色值只出现在 theme-*.json
	QString color(const QString& key) const;

	// QML 壳（ThemeBridge）用：当前模式的整份调色板（键 -> 色值）
	QHash<QString, QString> palette() const { return m_palette; }

	QString windowBg() const;
	QString border() const;
	QString textPrimary() const;
	QString textSecondary() const;
	QString inputBg() const;
	QString accent() const;

	// 用户显式选定主题的唯一入口：写 AppearanceSetting + setMode（M8 起不再重建窗口）
	void switchTheme(QWidget* currentWindow);

private:
	explicit ThemeManager();

	ThemeManager(const ThemeManager&) = delete;
	ThemeManager& operator=(const ThemeManager&) = delete;

	// 常量与无状态工具一律 private；刻意做成函数而非 static 成员，避免静态初始化期分配堆内存
	static QString modeKey(Mode mode);
	static QString token(const QHash<QString, QString>& palette, const QString& key, const QString& fallback);
	static void installPaletteFor(const QHash<QString, QString>& palette);

	QString paletteFileFor(Mode m) const;
	QString loadText(const QString& fileName) const;
	QHash<QString, QString> loadPaletteFor(Mode m) const;

	// 纯计算，供工作线程调用（只读调色板）
	QHash<QString, QString> buildFor(Mode m) const;

	void buildAllCaches();
	void ensureCached(Mode m);
	void activate(Mode m);

	QString m_stylesDir;
	Mode m_mode = Mode::Light;

	// 当前生效的调色板 + 两套模式的缓存（QSS 镜像已随 QSS 一起删除）
	QHash<QString, QString> m_palette;
	QHash<QString, QHash<QString, QString>> m_paletteCache;
};