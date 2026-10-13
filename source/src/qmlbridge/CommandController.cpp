// 见 CommandController.h：斜杠命令的发现 / 过滤 / 执行。

#include "qmlbridge/CommandController.h"

#include "network/DshApiClient.h"
#include "network/SessionCommands.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVariantMap>

namespace
{
	// 与引擎 dsh-commands/lib/index.js 的 COMMAND_NAME 同一条文法：
	// 斜杠在字节 0、小写名（字母/数字/_/-），后面必须是空白或行尾。
	// 捕获组 1 = 命令名；capturedEnd(0) 用来判断"名字后面有没有东西"。
	const QRegularExpression& commandLinePattern()
	{
		static const QRegularExpression pattern(QStringLiteral("^/([a-z][a-z0-9_-]*)(?:\\s|$)"));
		return pattern;
	}
}

CommandController::CommandController(DshApiClient* api, QObject* parent)
	: QObject(parent)
	, m_api(api)
{
}

bool CommandController::looksLikeCommand(const QString& line)
{
	return commandLinePattern().match(line).hasMatch();
}

QVariantList CommandController::matching(const QString& text) const
{
	// 注意：这里**不能用执行用的严格文法**（commandLinePattern 要求名字至少一个字符，且
	// `(?:\\s|$)` 会把空格吃进整段匹配）—— 两处都在试装台里被实测打脸过：
	//   · 只打一个 `/` 时名字还没开始，严格文法不匹配 ⇒ 菜单永不出现（而"打开命令列表"正是它的用途）；
	//   · 名字后打了空格时 `capturedEnd(0)` 已经越过空格 ⇒ 菜单赖着不走，挡住参数输入。
	// 所以这里单独解析：`/` + 名字前缀（可为空），一旦出现空白就说明名字已敲定，收菜单。
	if (!text.startsWith(QLatin1Char('/')))
		return QVariantList();

	int stop = -1;
	for (int i = 1; i < text.length(); ++i) {
		if (text.at(i).isSpace()) {
			stop = i;
			break;
		}
	}
	if (stop >= 0)
		return QVariantList();   // 名字已经敲定（后面是参数）

	const QString typed = text.mid(1);
	static const QRegularExpression prefix(QStringLiteral("^[a-z0-9_-]*$"));
	if (!prefix.match(typed).hasMatch())
		return QVariantList();   // 名字里出现了不可能的字符（大写等），别再猜

	QVariantList hits;
	for (const QVariant& value : m_items) {
		const QVariantMap item = value.toMap();
		if (item.value(QStringLiteral("name")).toString().startsWith(typed))
			hits.append(item);
	}
	return hits;
}

void CommandController::refresh(const QString& sessionId)
{
	if (!m_api || sessionId.isEmpty())
		return;
	m_listedSession = sessionId;   // 失败也记下：会话没换时不必反复拉
	m_api->callMethodValue(QStringLiteral("commands/list"), SessionCommands::commandsList(sessionId),
		[this, sessionId](const QJsonValue& value) {
			if (sessionId != m_listedSession)
				return;   // 期间换过会话，这份结果作废
			QVariantList items;
			for (const QJsonValue& entry : value.toArray()) {
				const QJsonObject object = entry.toObject();
				const QJsonObject input = object.value(QStringLiteral("input")).toObject();
				QVariantMap item;
				item.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
				item.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
				item.insert(QStringLiteral("hint"), input.value(QStringLiteral("hint")).toString());
				item.insert(QStringLiteral("takesAttachments"),
					input.value(QStringLiteral("attachments")).toBool());
				items.append(item);
			}
			setItems(items);
		},
		[this](const DshApiClient::RpcError& error) {
			qWarning().noquote() << "[Commands] commands/list 失败:" << error.code << error.message;
		});
}

void CommandController::execute(const QString& sessionId, const QString& line)
{
	if (!m_api || sessionId.isEmpty() || !looksLikeCommand(line))
		return;
	if (m_busy)
		return;   // 一次一条：命令通常很快，重复提交只会刷两条日志

	setBusy(true);
	m_api->callMethodValue(QStringLiteral("commands/execute"), SessionCommands::commandsExecute(sessionId, line),
		[this, line](const QJsonValue& value) {
			setBusy(false);
			if (!value.isObject()) {
				// 引擎对"语法像命令、但命令名不认识"回 undefined（dsh-commands：语法不合法或名字未知
				// 由适配器拒掉）。这正是原版那个 unknown command 分支，必须报出来，不能静默吞掉。
				const QString name = commandLinePattern().match(line).captured(1);
				emit executionFinished(false, qtTrId("command_unknown_fmt").arg(name));
				return;
			}
			const QJsonObject outcome = value.toObject().value(QStringLiteral("result")).toObject();
			const bool ok = outcome.value(QStringLiteral("kind")).toString() != QLatin1String("error");
			emit executionFinished(ok, outcome.value(QStringLiteral("text")).toString());
		},
		[this](const DshApiClient::RpcError& error) {
			setBusy(false);
			emit executionFinished(false, error.message);
		});
}

void CommandController::setBusy(bool busy)
{
	if (m_busy == busy)
		return;
	m_busy = busy;
	emit busyChanged();
}

void CommandController::setItems(const QVariantList& items)
{
	m_items = items;
	emit itemsChanged();
}
