#pragma once

// DSH RPC 请求载荷的统一构造入口（纯 header + inline）：JSON payload 拼装收敛到这里，控制器只负责编排。
// 约定：endpoint 是 `<namespace>/<method>`（斜杠，不是点号）；请求体恰好只有一个 `args` 对象，信封由 DshApiClient 包。
// 因此这里返回的都是 **args 的内容**，键名必须是描述符里的 wire 名（多数是 `request`，session/list 是 `_request`，
// 无参是空对象，agentPresets/select 是 `agentId` + `agentPreset`）。

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUuid>

namespace SessionCommands
{
	// 无参端点的 args：session/modelCatalog、agentPresets/list、llm/listProviders、
	// llm/listConfigurableProviders、settings/describe 等
	inline QJsonObject emptyArgs()
	{
		return QJsonObject();
	}

	// session/list 的 args：wire 名是 _request（描述符里参数名就叫 _request）
	inline QJsonObject sessionList()
	{
		QJsonObject args;
		args.insert(QStringLiteral("_request"), QJsonObject());
		return args;
	}

	// workspace/create -> args { request: { path } }
	inline QJsonObject workspaceCreate(const QString& path)
	{
		QJsonObject request;
		request.insert(QStringLiteral("path"), path);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// workspace/delete -> args { request: { workspaceId } }；只把工作区从服务端注册表里移除（含写盘），
	// 不碰它底下的会话。id 未知时服务端回 workspace/not-found。
	inline QJsonObject workspaceDelete(const QString& workspaceId)
	{
		QJsonObject request;
		request.insert(QStringLiteral("workspaceId"), workspaceId);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// session/create -> args { request: { workspaceId?, cwd?, sessionId?, agentPreset? } }
	inline QJsonObject sessionCreate(const QString& workspaceId = QString(), const QString& agentPreset = QString())
	{
		QJsonObject request;
		if (!workspaceId.isEmpty())
			request.insert(QStringLiteral("workspaceId"), workspaceId);
		if (!agentPreset.isEmpty())
			request.insert(QStringLiteral("agentPreset"), agentPreset);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// session/page -> args { request: { address, throughSeq, beforeSeq?, maxMessages? } }。throughSeq 必填（缺则回 gateway/input-invalid），
	// 取 follow 快照的 cursor 或 session/list 行的 projections.asOfSeq（两者相等）；不传 beforeSeq = 预取"throughSeq 往前最近一页"，
	// 传 = 首屏上翻（取当前内容里最早一条的 seq）；maxMessages 数的是**消息**而不是事件。
	inline QJsonObject sessionPage(const QString& sessionId, int throughSeq, int maxMessages = 0, int beforeSeq = 0)
	{
		QJsonObject address;
		address.insert(QStringLiteral("kind"), QStringLiteral("session"));
		address.insert(QStringLiteral("sessionId"), sessionId);

		QJsonObject request;
		request.insert(QStringLiteral("address"), address);
		request.insert(QStringLiteral("throughSeq"), throughSeq);
		if (maxMessages > 0)
			request.insert(QStringLiteral("maxMessages"), maxMessages);
		if (beforeSeq > 0)
			request.insert(QStringLiteral("beforeSeq"), beforeSeq);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// session/page 与 session/follow 的记录都是 {type:"event", event:{…}}，渲染路径只认裸事件对象，
	// 故统一在此取出内层事件。
	inline QJsonArray eventsFromRecords(const QJsonArray& records)
	{
		QSet<int> superseded;
		QSet<QString> supersededToolCalls;
		for (const auto& record : records) {
			const QJsonObject event = record.toObject().value(QStringLiteral("event")).toObject();
			const QJsonObject operation = event.value(QStringLiteral("surfaceOp")).toObject();
			if (operation.value(QStringLiteral("op")).toString() != QStringLiteral("replace"))
				continue;
			const int replacementSeq = event.value(QStringLiteral("seq")).toInt();
			const int startSeq = operation.value(QStringLiteral("startSeq")).toInt(-1);
			const int endSeq = operation.value(QStringLiteral("endSeq")).toInt(-1);
			const QJsonArray sources = event.value(QStringLiteral("sourceEventSeqs")).toArray();
			for (const auto& candidate : records) {
				const QJsonObject oldEvent = candidate.toObject().value(QStringLiteral("event")).toObject();
				const int seq = oldEvent.value(QStringLiteral("seq")).toInt();
				bool replaced = seq < replacementSeq && startSeq >= 0 && endSeq >= startSeq
					&& seq >= startSeq && seq <= endSeq;
				for (const auto& source : sources)
					replaced = replaced || (seq < replacementSeq && source.toInt(-1) == seq);
				if (!replaced)
					continue;
				superseded.insert(seq);
				if (oldEvent.value(QStringLiteral("type")).toString() == QStringLiteral("tool/result")) {
					const QJsonObject message = oldEvent.value(QStringLiteral("data")).toObject()
						.value(QStringLiteral("message")).toObject();
					const QString callId = message.value(QStringLiteral("toolCallId")).toString();
					if (!callId.isEmpty())
						supersededToolCalls.insert(callId);
				}
			}
		}

		QJsonArray events;
		for (const auto& record : records) {
			QJsonObject event = record.toObject().value(QStringLiteral("event")).toObject();
			if (event.isEmpty() || superseded.contains(event.value(QStringLiteral("seq")).toInt()))
				continue;
			if (event.value(QStringLiteral("type")).toString() == QStringLiteral("tool/call")
				&& supersededToolCalls.contains(event.value(QStringLiteral("data")).toObject()
					.value(QStringLiteral("callId")).toString()))
				continue;

			if (event.value(QStringLiteral("type")).toString() == QStringLiteral("tool/result")) {  // 服务端发的是结构化 message 对象，旧渲染器只吃字符串
				QJsonObject data = event.value(QStringLiteral("data")).toObject();
				const QJsonValue messageValue = data.value(QStringLiteral("message"));
				if (messageValue.isObject()) {
					const QJsonObject message = messageValue.toObject();
					QString text = message.value(QStringLiteral("content")).toString();
					if (text.isEmpty()) {
						const QJsonArray blocks = message.value(QStringLiteral("content")).toArray();
						QStringList parts;
						for (const auto& blockValue : blocks) {
							const QJsonObject block = blockValue.toObject();
							const QString blockText = block.value(QStringLiteral("text")).toString();
							parts.append(blockText.isEmpty()
								? QString::fromUtf8(QJsonDocument(block).toJson(QJsonDocument::Compact))
								: blockText);
						}
						text = parts.join(QLatin1Char('\n'));
					}
					const QString reason = data.value(QStringLiteral("error")).toObject()
						.value(QStringLiteral("reason")).toString();
					if (message.value(QStringLiteral("isError")).toBool() && !reason.isEmpty())
						text = QStringLiteral("[Tool error] %1\n%2").arg(reason, text);
					if (data.contains(QStringLiteral("meta")))
						text += QStringLiteral("\n\n[Tool details]\n%1").arg(QString::fromUtf8(
							QJsonDocument(data.value(QStringLiteral("meta")).toObject()).toJson(QJsonDocument::Indented)));
					data.insert(QStringLiteral("message"), text);
					event.insert(QStringLiteral("data"), data);
				}
			}
			if (event.value(QStringLiteral("type")).toString() == QStringLiteral("turn/end")) {
				const QJsonObject data = event.value(QStringLiteral("data")).toObject();
				const QJsonObject reason = data.value(QStringLiteral("reason")).toObject();
				if (reason.value(QStringLiteral("kind")).toString() == QStringLiteral("error")) {
					const QJsonObject failure = reason.value(QStringLiteral("error")).toObject();
					const QString code = failure.value(QStringLiteral("code")).toString();
					const QString detail = failure.value(QStringLiteral("message")).toString();
					const QString text = code.isEmpty()
						? QStringLiteral("[Request failed] %1").arg(detail)
						: QStringLiteral("[Request failed: %1] %2").arg(code, detail);
					QJsonObject message;
					message.insert(QStringLiteral("content"), text);
					QJsonObject legacyData;
					legacyData.insert(QStringLiteral("message"), message);
					event.insert(QStringLiteral("type"), QStringLiteral("assistant/message"));
					event.insert(QStringLiteral("data"), legacyData);
				}
			}
			event.remove(QStringLiteral("surfaceOp"));
			event.remove(QStringLiteral("sourceEventSeqs"));
			events.append(event);
		}
		return events;
	}

	inline QJsonArray legacyRecords(const QJsonArray& records)
	{
		QJsonArray normalized;
		for (const auto& event : eventsFromRecords(records)) {
			QJsonObject record;
			record.insert(QStringLiteral("type"), QStringLiteral("event"));
			record.insert(QStringLiteral("event"), event);
			normalized.append(record);
		}
		return normalized;
	}

	// session/prompt -> args { request: { requestId, sessionId, mode, content } }；requestId 必填，由客户端铸 id、
	// 服务端把它落到那条用户消息上。
	inline QJsonObject sessionPrompt(const QString& sessionId, const QString& text)
	{
		QJsonArray content;
		QJsonObject textPart;
		textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
		textPart.insert(QStringLiteral("text"), text);
		content.append(textPart);

		QJsonObject request;
		request.insert(QStringLiteral("requestId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
		request.insert(QStringLiteral("sessionId"), sessionId);
		request.insert(QStringLiteral("mode"), QStringLiteral("queue"));
		request.insert(QStringLiteral("content"), content);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// session/cancel -> args { request: { sessionId } }
	inline QJsonObject sessionCancel(const QString& sessionId)
	{
		QJsonObject request;
		request.insert(QStringLiteral("sessionId"), sessionId);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// workspace/archiveSession（删除会话）-> args { request: { sessionId } }
	inline QJsonObject sessionArchive(const QString& sessionId)
	{
		QJsonObject request;
		request.insert(QStringLiteral("sessionId"), sessionId);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// agentPresets/select -> args { agentId, agentPreset }（不是 `request` 包装，agentId 就是 sessionId）；
	// 只在会话空白期可用，跑过任一轮即永久锁定（服务端回 agent-preset/locked）；Hub 暂无调用点，保留作协议收录。
	inline QJsonObject agentPresetSelect(const QString& sessionId, const QString& presetId)
	{
		QJsonObject args;
		args.insert(QStringLiteral("agentId"), sessionId);
		args.insert(QStringLiteral("agentPreset"), presetId);
		return args;
	}

	// session/selectModel -> args { request: { sessionId, provider, model, reasoningEffort? } }；
	// 省略 reasoningEffort = 用提供方默认档位。
	inline QJsonObject sessionSelectModel(const QString& sessionId, const QString& provider, const QString& model, const QString& reasoningEffort = QString())
	{
		QJsonObject request;
		request.insert(QStringLiteral("sessionId"), sessionId);
		request.insert(QStringLiteral("provider"), provider);
		request.insert(QStringLiteral("model"), model);
		if (!reasoningEffort.isEmpty())
			request.insert(QStringLiteral("reasoningEffort"), reasoningEffort);

		QJsonObject args;
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// settings/mutate -> args { ns, ops, expectedRevision? }
	inline QJsonObject settingsMutate(const QString& ns, const QJsonArray& ops)
	{
		QJsonObject args;
		args.insert(QStringLiteral("ns"), ns);
		args.insert(QStringLiteral("ops"), ops);
		return args;
	}

	// settings/update -> args { ns, patch, expectedRevision? }；把 patch 浅合并进该 ns 的 user 段（与 mutate 的按
	// 路径寻址不同）；整条省掉 expectedRevision 即无条件写（描述符标了 acceptsUndefined，不做乐观并发校验）。
	inline QJsonObject settingsUpdate(const QString& ns, const QJsonObject& patch)
	{
		QJsonObject args;
		args.insert(QStringLiteral("ns"), ns);
		args.insert(QStringLiteral("patch"), patch);
		return args;
	}

	// llm/discoverModels -> args { settingsNs, request: { provider?, baseURL?, api?, apiKey? } }（两个平铺参数，不是
	// `request` 包一层）；settingsNs 决定由哪个命名空间的适配器回答，回包是**裸数组**
	// [{ id, name?, contextWindow?, maxTokens? }]，调用方要用 callMethodValue 接。
	inline QJsonObject llmDiscoverModels(const QString& settingsNs, const QJsonObject& request)
	{
		QJsonObject args;
		args.insert(QStringLiteral("settingsNs"), settingsNs);
		args.insert(QStringLiteral("request"), request);
		return args;
	}

	// credentials/describe -> args { refs: [ref, …] }
	inline QJsonObject credentialsDescribe(const QJsonArray& refs)
	{
		QJsonObject args;
		args.insert(QStringLiteral("refs"), refs);
		return args;
	}

	// credentials/set -> args { ref, value }
	inline QJsonObject credentialsSet(const QString& ref, const QString& value)
	{
		QJsonObject args;
		args.insert(QStringLiteral("ref"), ref);
		args.insert(QStringLiteral("value"), value);
		return args;
	}

	// ---- 斜杠命令（commands）----
	// 两个端点的 scope 是 { context: 'agent', wire: 'agentId' } ⇒ args 顶层就是 `agentId`，
	// **不套 `request`**（与上面的 agentPresets/select 同形）。依据：引擎生成的远程客户端桩
	// `@deepseek-ai/dsh-commands/lib/typert.remote-client.js`（0.1.7 与 0.2.0 逐字段一致）。
	// commands/list -> args { agentId }
	inline QJsonObject commandsList(const QString& sessionId)
	{
		QJsonObject args;
		args.insert(QStringLiteral("agentId"), sessionId);
		return args;
	}

	// commands/execute -> args { agentId, line, submittedAttachments }
	// `line` 是**整条斜杠命令行**（含前导斜杠与参数原文），解析在服务端；attachments 目前恒为空数组
	// （本客户端还没有附件上传；该键必填，省略会被严格 codec 拒掉）。
	inline QJsonObject commandsExecute(const QString& sessionId, const QString& line)
	{
		QJsonObject args;
		args.insert(QStringLiteral("agentId"), sessionId);
		args.insert(QStringLiteral("line"), line);
		args.insert(QStringLiteral("submittedAttachments"), QJsonArray());
		return args;
	}
}
