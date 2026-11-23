#include "common/util/Debugger.h"
#include "common/util/Format.h"
#include "common/util/Json.h"
#include "common/session/EventRecord.h"
#include "common/session/ChatController.h"
#include "common/session/MessageListModel.h"
#include "common/agent/AgentSessionClient.h"
#include "common/session/TraceModel.h"

#include "common/agent/dsh/DshApiClient.h"
#include "common/agent/AgentEventParser.h"
#include "common/util/ConnectionManager.h"
#include "network/SessionCommands.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include "common/session/chat/ChatControllerInternal.h"

using namespace chatctl;

void ChatController::onJobRoster(const QJsonArray& jobs)
{
	struct Row
	{
		bool live = false;
		qint64 startedAt = 0;
		qint64 finishedAt = 0;
		QVariantMap map;
	};

	QVector<Row> rows;
	rows.reserve(jobs.size());
	for (const QJsonValue& value : jobs) {
		const QJsonObject job = value.toObject();
		const QString id = job.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
			continue;   // 没有 id 的行无处可点、也没法停，直接丢

		const QString status = job.value(QStringLiteral("status")).toString();

		Row row;
		row.live = (status == QLatin1String("running") || status == QLatin1String("stopping"));
		row.startedAt = static_cast<qint64>(job.value(QStringLiteral("startedAt")).toDouble());
		row.finishedAt = static_cast<qint64>(job.value(QStringLiteral("finishedAt")).toDouble(row.startedAt));

		row.map.insert(QStringLiteral("id"), id);
		row.map.insert(QStringLiteral("kind"), job.value(QStringLiteral("kind")).toString());
		row.map.insert(QStringLiteral("label"), job.value(QStringLiteral("label")).toString());
		row.map.insert(QStringLiteral("status"), status);
		row.map.insert(QStringLiteral("progress"), job.value(QStringLiteral("progress")).toString());
		row.map.insert(QStringLiteral("detail"), job.value(QStringLiteral("detail")).toString());
		row.map.insert(QStringLiteral("live"), row.live);
		row.map.insert(QStringLiteral("startedAt"), row.startedAt);
		row.map.insert(QStringLiteral("finishedAt"), row.finishedAt);
		rows.append(row);
	}

	std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
		if (left.live != right.live)
			return left.live;                       // 活的排在已结束的前面
		if (left.live)
			return left.startedAt < right.startedAt; // 都是活的：先开始的在前
		return left.finishedAt > right.finishedAt;   // 都结束了：最近结束的在前
	});

	QVariantList list;
	list.reserve(rows.size());
	for (const Row& row : rows)
		list.append(row.map);

	m_jobs = list;
	emit jobsChanged();
}

bool ChatController::jobRunning() const
{
	for (const QVariant& value : m_jobs) {
		const QString status = value.toMap().value(QStringLiteral("status")).toString();
		if (status == QLatin1String("running") || status == QLatin1String("stopping"))
			return true;
	}
	return false;
}

void ChatController::handleQuestionFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QJsonArray questions = payload.value(QStringLiteral("questions")).toArray();

	if (rpcId.isEmpty() || sessionId.isEmpty() || questions.isEmpty()) {
		qWarning().noquote() << "[Chat] question/requested 帧无效，已忽略";
		return;
	}

	QVariantList rows;
	for (const QJsonValue& questionValue : questions) {
		const QJsonObject question = questionValue.toObject();
		QVariantMap row;
		row.insert(QStringLiteral("id"), question.value(QStringLiteral("id")).toString());
		row.insert(QStringLiteral("header"), question.value(QStringLiteral("header")).toString());
		row.insert(QStringLiteral("question"), question.value(QStringLiteral("question")).toString());
		row.insert(QStringLiteral("multiSelect"), question.value(QStringLiteral("multiSelect")).toBool());

		QVariantList options;
		for (const QJsonValue& optionValue : question.value(QStringLiteral("options")).toArray()) {
			const QJsonObject option = optionValue.toObject();
			QVariantMap item;
			item.insert(QStringLiteral("label"), option.value(QStringLiteral("label")).toString());
			item.insert(QStringLiteral("description"), option.value(QStringLiteral("description")).toString());
			options.append(item);
		}
		row.insert(QStringLiteral("options"), options);
		rows.append(row);
	}

	m_questionRpcId = rpcId;
	m_questionSessionId = sessionId;
	m_questions = rows;
	resetAnswerSelections();
	qInfo().noquote() << "[Chat] question/requested 提问数=" << rows.size() << " session=" << sessionId;
	emit interactionChanged();
}

void ChatController::handleApprovalFrame(const QJsonObject& frame)
{
	const QJsonObject payload = frame.value(QStringLiteral("payload")).toObject();
	const QString rpcId = frame.value(QStringLiteral("rpcId")).toString();
	const QString sessionId = payload.value(QStringLiteral("sessionId")).toString();
	const QString approvalId = payload.value(QStringLiteral("approvalId")).toString();

	// approval/request 不带 approvalId，只要求 eventId + sessionId —— 不能拿 approvalId 非空做有效性门槛，
	// 否则每一条审批都会被静默丢掉。
	if (rpcId.isEmpty() || sessionId.isEmpty()) {
		qWarning().noquote() << "[Chat] approval/requested 帧无效，已忽略";
		return;
	}

	m_approvalRpcId = rpcId;
	m_approvalSessionId = sessionId;
	m_approvalId = approvalId;
	m_approvalToolName = payload.value(QStringLiteral("toolName")).toString();
	m_approvalReason = payload.value(QStringLiteral("reason")).toString();
	qInfo().noquote() << "[Chat] approval/requested 工具=" << m_approvalToolName;
	emit interactionChanged();
}

void ChatController::resetAnswerSelections()
{
	QVariantList selections;
	for (const QVariant& value : m_questions) {
		const QVariantMap question = value.toMap();
		QVariantList selected;
		const QVariantList options = question.value(QStringLiteral("options")).toList();
		// 与旧 QML 行为一致：单选且只有一个选项时默认选中。
		if (options.size() == 1 && !question.value(QStringLiteral("multiSelect")).toBool())
			selected.append(options.first().toMap().value(QStringLiteral("label")).toString());

		QVariantMap row;
		row.insert(QStringLiteral("id"), question.value(QStringLiteral("id")).toString());
		row.insert(QStringLiteral("selected"), selected);
		row.insert(QStringLiteral("custom"), QString());
		selections.append(row);
	}
	m_answerSelections = selections;
	emit answerSelectionsChanged();
}

void ChatController::toggleAnswerOption(int questionIndex, const QString& label)
{
	if (questionIndex < 0 || questionIndex >= m_questions.size()
		|| questionIndex >= m_answerSelections.size())
		return;

	const bool multi = m_questions.at(questionIndex).toMap()
		.value(QStringLiteral("multiSelect")).toBool();
	QVariantMap row = m_answerSelections.at(questionIndex).toMap();
	QVariantList selected = row.value(QStringLiteral("selected")).toList();

	const int at = selected.indexOf(label);
	if (multi) {
		if (at >= 0)
			selected.removeAt(at);
		else
			selected.append(label);
	} else {
		selected.clear();
		// 再点一次已选中的唯一选项 = 取消选择，保持旧面板行为。
		if (at < 0)
			selected.append(label);
	}
	row.insert(QStringLiteral("selected"), selected);
	m_answerSelections[questionIndex] = row;
	emit answerSelectionsChanged();
}

void ChatController::setAnswerCustom(int questionIndex, const QString& text)
{
	if (questionIndex < 0 || questionIndex >= m_answerSelections.size())
		return;
	QVariantMap row = m_answerSelections.at(questionIndex).toMap();
	row.insert(QStringLiteral("custom"), text);
	m_answerSelections[questionIndex] = row;
	emit answerSelectionsChanged();
}

void ChatController::submitAnswerSelections()
{
	submitAnswers(m_answerSelections);
}

void ChatController::submitAnswers(const QVariantList& answers)
{
	if (m_questionRpcId.isEmpty() || !m_api)
		return;

	QJsonArray answerArray;
	for (const QVariant& value : answers) {
		const QVariantMap row = value.toMap();
		QJsonObject answer;
		answer.insert(QStringLiteral("id"), row.value(QStringLiteral("id")).toString());

		QJsonArray selected;
		for (const QVariant& label : row.value(QStringLiteral("selected")).toList())
			selected.append(label.toString());
		answer.insert(QStringLiteral("selected"), selected);

		const QString custom = row.value(QStringLiteral("custom")).toString().trimmed();
		if (!custom.isEmpty())
			answer.insert(QStringLiteral("custom"), custom);

		answerArray.append(answer);
	}

	QJsonObject answer;
	answer.insert(QStringLiteral("answers"), answerArray);

	// result 就是 { answers:[…]}，没有外层 { sessionId, answer } 信封。
	m_api->respond(m_questionRpcId, answer);

	const QString rpcId = m_questionRpcId;  // 答完即收起（服务端不会就同一帧再问一次）
	m_questionRpcId.clear();
	m_questionSessionId.clear();
	m_questions.clear();
	m_answerSelections.clear();
	emit answerSelectionsChanged();
	qInfo().noquote() << "[Chat] question 已应答 rpcId=" << rpcId;
	emit interactionChanged();
}

void ChatController::respondApproval(bool allow)
{
	if (m_approvalRpcId.isEmpty() || !m_api)
		return;

	// approval waterfall 的 result 直接是 outcome 字符串，不是对象。
	m_api->respondValue(m_approvalRpcId, QJsonValue(allow ? QStringLiteral("allowed-once") : QStringLiteral("rejected")));

	const QString rpcId = m_approvalRpcId;
	m_approvalRpcId.clear();
	m_approvalSessionId.clear();
	m_approvalId.clear();
	m_approvalToolName.clear();
	m_approvalReason.clear();
	qInfo().noquote() << "[Chat] approval 已应答 rpcId=" << rpcId << " allow=" << allow;
	emit interactionChanged();
}
