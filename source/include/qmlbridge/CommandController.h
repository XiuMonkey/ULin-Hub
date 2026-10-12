#pragma once

// 斜杠命令处理器（QML 面：`Hub.commands`）—— 发现 / 过滤 / 执行。
//
// 为什么单独一个类：命令**不进模型历史**。引擎侧 `execute()` 只往接收方会话日志追加
// `command/run` + `command/done` 两条生命周期事件，不产生任何模型消息（见 dsh-commands README：
// "run directly against an agent without creating a model message"）—— 所以它既不属于
// ChatController 的"发一轮提示"，也不属于任何设置面，单独一层最省事。
//
// 引擎契约（随包 0.1.7-rc.2 与官方 0.2.0-rc.2 **逐字段一致**；依据是引擎生成的远程客户端桩
// `@deepseek-ai/dsh-commands/lib/typert.remote-client.js`）：
//   · commands/list    args { agentId }                        -> [{ name, description, input?: { hint, attachments? } }]
//   · commands/execute args { agentId, line, submittedAttachments }
//                                                              -> undefined（语法不对/命令名不认识）| { commandId, result }
//   · result = { kind: 'success', text?, sourceEventSeq? } | { kind: 'error', text }
//   · 命令名文法同引擎 COMMAND_NAME：`^/[a-z][a-z0-9_-]*(\s|$)`
//
// ⚠️ sessionId 一律**入参**（与 ChatController 同一约定）：当前会话归 qmlbridge 所有，
//    本类只记"上一次给哪个会话拉过列表"，用来丢弃换会话之后才回来的旧响应。

#include <QObject>
#include <QString>
#include <QVariantList>

class DshApiClient;

class CommandController : public QObject
{
	Q_OBJECT
	// 当前会话可用的命令：[{ name, description, hint, takesAttachments }]
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	// 一行里有候选（Composer 据此决定要不要弹菜单）。这里只做文法判定，不认识命令名也算 true ——
	// "认不认识"由引擎裁定（execute 回 undefined），本地不维护第二份命令表。
	Q_PROPERTY(bool available READ available NOTIFY itemsChanged)

public:
	explicit CommandController(DshApiClient* api, QObject* parent = nullptr);

	QVariantList items() const { return m_items; }
	bool busy() const { return m_busy; }
	bool available() const { return !m_items.isEmpty(); }

	// 命令行文法（与引擎 parseCommand 同一正则）：斜杠在字节 0，小写名，后接空白或行尾。
	// 静态版给 C++ 用（QmlBridge 判"这条到底走命令还是走模型"），QML 版给输入区用。
	static bool looksLikeCommand(const QString& line);
	Q_INVOKABLE bool isCommandLine(const QString& line) const { return looksLikeCommand(line); }

	// 当前输入命中的候选：只在"还在敲命令名"时返回（`/com` 会过滤，`/compact ` 之后不再弹菜单）。
	Q_INVOKABLE QVariantList matching(const QString& text) const;

	// 拉当前会话的命令表（会话切换后、或用户刚打出 `/` 时调）
	Q_INVOKABLE void refresh(const QString& sessionId);
	// 执行整条命令行（含参数原文）
	Q_INVOKABLE void execute(const QString& sessionId, const QString& line);

signals:
	void itemsChanged();
	void busyChanged();
	// 一次执行收尾。ok=false 时 text 是引擎给的失败原因，或本地判定的"不认识的命令"。
	void executionFinished(bool ok, const QString& text);

private:
	void setBusy(bool busy);
	void setItems(const QVariantList& items);

	DshApiClient* m_api = nullptr;
	QVariantList m_items;
	QString m_listedSession;   // 最近一次成功发起 list 的会话（用于丢弃过期响应）
	bool m_busy = false;
};
