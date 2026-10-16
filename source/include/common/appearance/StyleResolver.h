#pragma once

// 分层（领域归位 2026-10-15）：本文件在 common/appearance/ —— **领域层**（风格包目录的解析与
// uiAbi 门控、磁盘包遮蔽/回退的判定、URL 缓存都在这里，与它下游的 ThemeManager 同层）。
// 它不 import 任何 QML 概念（不 include QQmlEngine，只吐字符串 URL），可以脱离 QML 引擎单测。
// QML 面（单例名 `Style`、类型注册、启动期 init）在 src/core/main.cpp，那边只接线、不放算法。
//
// QML 风格包（对应 docs/ui-qml-migration-plan.md §2.4 / 决策 D4 的三层覆盖）：
//   A 层 token …… ThemeBridge（颜色来自 theme-*.json，尺寸/字号来自 metrics.json）
//   B 层部件文件 … 本类：Style.url("chat/AgentBubble.qml") —— 磁盘包同名文件**遮蔽**内置文件，
//                    没覆盖的自动回退内置
//   C 层私有件 … 包内文件自己的相对 import 相对**包目录**解析（QML 引擎负责，本类不管）
//
// 首启把内置界面树（= 内置 QML 整棵树 + pack.json + metrics.json）从 qrc 释放到 <exe>/ui/，
// 规则是**增量同步**（2026-10-01 起，见 ThemeManager::ensureStylePack）：
//   包内没有 → 释放；用户改过的 → 不动；用户没改而内置更新过的 → 覆盖。
// 基线记在同目录的 release-manifest.json（相对路径 → 上次释放时的内容哈希）。
//
// 升级陈旧化：磁盘包里的 QML 会遮蔽新内置件，而 QML 陈旧（模型 role/属性改名）会运行期报错 ——
// 所以 pack.json 带 uiAbi，与本版本的 kStylePackUiAbi 不一致就**整包拒载、全部回退内置**
// （拒载原因只留在 m_rejectReason 与警告日志里，不再往 QML 面暴露）。

// QML 壳与风格包之间的契约版本早就搬到了 common/appearance/UiAbi.h —— 它同时被下层的
// ThemeManager / ExtensionLoader 使用，留在桥层会让下层反向依赖 QML 桥层。
// 名字 kStylePackUiAbi 保留（旧代码都在用），值由那份头定义。
#include "common/appearance/UiAbi.h"

#include <QHash>
#include <QObject>
#include <QString>

class StyleResolver : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString packVersion READ packVersion NOTIFY packChanged)
	Q_PROPERTY(QString stylesDir READ stylesDir CONSTANT)

public:
	// 程序级唯一实例；刻意不析构 —— QML 单例注册要求它活过引擎
	static StyleResolver& instance();

	// 启动期（任何 QML 装载之前）调用：释放内置包 + 做 uiAbi 门控
	void init(const QString& stylesDir);

	// QML 侧唯一入口：逻辑路径 -> 可加载 URL（磁盘包优先 / qrc 兜底），结果缓存
	Q_INVOKABLE QString url(const QString& logicalPath) const;
	// 重置：删掉磁盘上的包文件（含 pack.json），再重新释放内置包（theme-*.json / *.qss 不动）
	Q_INVOKABLE bool reset();
	// 重新做门控与缓存失效（用户/扩展改过磁盘包后调用）
	Q_INVOKABLE void reload();

	QString packVersion() const { return m_packVersion; }
	QString stylesDir() const { return m_stylesDir; }

signals:
	void packChanged();

private:
	explicit StyleResolver(QObject* parent = nullptr);
	StyleResolver(const StyleResolver&) = delete;
	StyleResolver& operator=(const StyleResolver&) = delete;

	// 把内置包（qrc:/qml 整棵树）释放到 stylesDir：已存在的文件不覆盖
	void ensureDefaults() const;
	// 读 pack.json 做 uiAbi 门控；缺文件 = 没有磁盘包（不是被拒）
	void inspectPack();
	static QString builtinUrl(const QString& logicalPath);

	QString m_stylesDir;
	mutable QHash<QString, QString> m_urlCache;
	// ⚠️ 下面三个是**解析期的内部状态**（inspectPack 写、url()/日志读），刻意保留：
	//    它们不是死代码，只是不再有 QML 面（Q_PROPERTY/getter 已删）。
	bool m_packPresent = false;
	bool m_packAccepted = true;
	QString m_packVersion;
	QString m_rejectReason;
};
