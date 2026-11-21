# ulincore —— 自研 Agent 内核（雏形）

`ulincore` 是 ULin Hub 自己的 Agent 内核，不依赖 DSH 的会话/事件协议。
目标是把「一轮 Agent 怎么跑」这件事拿回自己手里：

- 模型只负责补全 + 工具调用；
- 内核负责消息账本、工具执行、步数上限、取消、用量累计、事件输出；
- 传输、模型厂商、工具集合都可以替换。

当前是**可编译、可跑通回路的实现**：已通过 `UlinCoreMasterAgent` 注册进 Agent 注册表，
并接入 OpenAI 兼容的 `UlinCoreOpenAiModel`；`data/ulincore/settings.json` 里没有 apiKey 时
会退回 `UlinCoreEchoModel`，方便先验证回路。

## 目录与职责

```
include/shared/common/agent/ulincore/
  types/
    UlinCoreTypes.h         Message / ToolCall / ToolResult / Usage / TurnResult
  intent/
    UlinCoreIntent.h        统一 Intent 数据模型
    UlinCoreInvocation.h    Intent 调用/结果
    UlinCoreIntentStore.h   Intent 持久化
    UlinCoreParser.h        提示词/Intent 解析
    UlinCoreBinding.h       参数绑定
  model/
    UlinCoreModel.h         模型适配层接口（流式增量 + 工具调用 + 用量）
    UlinCoreEchoModel.h     无网络确定性模型，用于回环测试
    UlinCoreOpenAiModel.h   OpenAI 兼容流式模型（/chat/completions + SSE）
  tool/
    UlinCoreTool.h          Tool 接口 + 注册表（OpenAI function schema）
    UlinCoreBuiltinTools.h  read_file / list_dir / write_file / search_text
    UlinCoreRecipeTool.h    Recipe 工具
  core/
    UlinCoreSession.h       一次会话的消息账本 + snapshot/restore
    UlinCoreKernel.h        内核状态机
    UlinCoreJournal.h       JSONL 事件账本
    UlinCoreMasterAgent.h   Master Agent 注册 + DSH 出站适配
  settings/
    UlinCoreSettings.h      data/ulincore/settings.json 的设置门面

src/shared/common/agent/ulincore/
  按同名的 types / intent / model / tool / core / settings 分目录
```

## 统一 Intent 模型

Prompt、Chain、原生工具不是三种类，而是同一个 `Intent` 的三种能力：

```text
prompt 非空    -> Prompt 执行
steps 非空     -> Chain 执行
executor 非空  -> 客户端原生执行器
```

JSON：

```json5
{
  name: 'read_file',
  description: '读取文件',
  parameters: [{ name: 'path', type: 'string' }],
  prompt: '',
  steps: [],
  executor: 'builtin.read_file',
}
```

`prompt + steps` 可以同时存在：默认走 Chain，调用时用 `AsPrompt: true` 强行走 Prompt。

`IntentStep` 引用另一个 Intent：

```json
{ "intent": "read_file", "arguments": { "path": { "$param": "path" } } }
```

`IntentStep` 只使用 `intent` 字段，不再兼容旧字段 `tool`。

## 一轮 turn 的时序

```
run(userText)
  └─ session.append(User)
     └─ startStep()
        └─ model.start(messages + tools)
           ├─ onDelta        → assistantDelta
           └─ onComplete
              ├─ 无 toolCalls → session.append(Assistant) → turnFinished(ok)
              └─ 有 toolCalls
                 ├─ session.append(Assistant + toolCalls)
                 ├─ 逐个 execute() → session.append(Tool)
                 └─ startStep() 再问模型（直到无 toolCalls / 步数上限）
```

关键约束：

- `m_generation` 代次号让「取消/新一轮」之后旧模型的回调全部失效；
- `startStep()` 用 `QTimer::singleShot(0, …)` 异步起跑，避免模型同步回调造成递归；
- 工具结果是 `Role::Tool` 消息，带 `toolCallId`，模型适配层要把它翻译成厂商的 tool result；
- 单轮步数上限 `Options::maxSteps`，工具结果按 `maxToolResultChars` 截断。

## 最小用法

```cpp
#include "common/agent/ulincore/core/UlinCoreKernel.h"
#include "common/agent/ulincore/model/UlinCoreEchoModel.h"

auto* kernel = new UlinCore::UlinCoreKernel(parent);
kernel->setModel(std::make_shared<UlinCore::UlinCoreEchoModel>());

UlinCore::UlinCoreKernel::Options options;
options.systemPrompt = QStringLiteral("You are ULin.");
options.toolContext.workspaceRoot = projectRoot;
options.toolContext.allowWrites = false;
kernel->setOptions(options);

connect(kernel, &UlinCore::UlinCoreKernel::assistantDelta, this,
    [](const QString&, const QString& text, const QString&) { /* 流式上屏 */ });
connect(kernel, &UlinCore::UlinCoreKernel::turnFinished, this,
    [](const UlinCore::TurnResult& result) { /* 收尾 */ });

kernel->run(QStringLiteral("hello"));
```

跑工具回路（Echo 模型第一步先发一次 `list_dir`）：

```cpp
auto echo = std::make_shared<UlinCore::UlinCoreEchoModel>();
echo->setScriptedToolCall(QStringLiteral("list_dir"), QJsonObject{
    { QStringLiteral("path"), QStringLiteral(".") }
});
kernel->setModel(echo);
kernel->run(QStringLiteral("看看目录"));
```

## 模型适配层契约

实现 `UlinCoreModel` 时只需要：

1. `start(request, callbacks)`：把 `request.messages` 翻成厂商格式；
   - `Role::System` → system prompt；
   - `Role::User` → user；
   - `Role::Assistant` + `toolCalls` → assistant 的 tool_calls；
   - `Role::Tool` → `tool_call_id` + content。
2. 流式文本通过 `callbacks.onDelta(text, thinking)`；
3. 结束时 `callbacks.onComplete(calls, fullText, fullThinking, usage, finishReason)`；
4. 出错 `callbacks.onError(message)`；
5. `cancel()` 要尽快停止网络请求，并保证之后不再回调（内核还有代次号兜底）。

> 真实实现可以走 OpenAI 兼容的 `/chat/completions`，也可以走 DSH 的 session/prompt；
> 内核不关心。建议把厂商字段（`tool_calls[].function.arguments` 的 JSON 字符串）
> 在适配层里就解析成 `ToolCall.arguments`，别把 wire 细节漏进内核。

## 工具与安全

内置工具：

| 工具 | 作用 | 默认 |
|---|---|---|
| `read_file` | 读工作区内 UTF-8 文本，`maxBytes` 封顶 1MB | 开 |
| `list_dir` | 列目录，`maxEntries` 封顶 2000 | 开 |
| `write_file` | 写工作区内文本 | **关**（`allowWrites=false`） |
| `search_text` | 字面量搜索，跳过 `.git/node_modules/x64/build/.vs` | 开 |

路径统一用 `resolveInside()` 钉在 `workspaceRoot` 里，`..`、绝对路径越界都会拒绝。
`allowShell` 是给以后 `run_shell` 预留的开关，当前没有实现 shell 工具。

## 与 DSH 的关系

现在 `dsh/` 是「DSH 协议适配层」，`ulincore/` 是「内核」；两者刻意分开：

- 短期：`ulincore` 可以接一个 `UlinCoreDshModel`，复用现有后端，但会话账本/工具策略由自己管；
- 中期：把 DSH 的 `tool/call`、`assistant/message` 翻译成 `UlinCore::Message`，做双跑对比；
- 长期：模型直连（OpenAI 兼容 / 本地模型），DSH 只作为可选后端之一。

## 注册成 Master Agent 后端

`UlinCoreMasterAgent` 实现 `VirtualMasterAgent` + `VirtualApiSink`，由 `ClientRuntime` 在启动时注册：

```cpp
m_ulinCoreAgent = new UlinCore::UlinCoreMasterAgent(m_api, m_api, this);
m_api->RegisterMasterAgent("ulincore", m_ulinCoreAgent);
```

- 设置页的 Agent 列表会多出 “ULin Core”（和 CodexBackend / WorkBuddy 同一张注册表）；
- 选中后 `ActivateMasterAgent()` 调 `Takenover(true)`，出站 `session/prompt` 由本类接；
- `OnOutboundRequest` 把 `session/prompt` 转成 `kernel->run()`，再把内核信号翻成
  `session/event` 帧（`assistant/chunk`、`tool/call`、`tool/result`、`turn/end`）；
- `session/follow` 回放本地账本；`session/cancel` 转 `kernel->cancel()`；
- `session/create` / `session/list` / `workspace/create` 的会话与分组落盘在
  `<exe>/data/ulincore/sessions.json`（QSaveFile 原子写），进程重启会恢复；
  `session/delete`、`workspace/archiveSession` 等只做最小响应。

## 本地数据（`<exe>/data/ulincore`）

- `settings.json` — `providers[]`（每个 provider 带 `baseUrl` / `apiKey` / `models[]`）、
  `selectedProvider` / `selectedModel`、`systemPrompt`、`workspaceRoot`、
  `toolThreads` / `maxSteps` / `allowWrites`；首次启动自动写默认 provider。
- `launch-root/` — 工具与新建会话的默认工作目录。
- `sessions.json` — 会话/分组元数据（标题、cwd、workspace、时间）。
- `sessions/<id>.json` — 每个会话的本地账本：`turn/start`、`user/message`、
  `assistant/message`、`tool/call`、`tool/result`、`turn/end`；`session/follow` 时回放。

要接真实模型：在设置页“提供商”里选中 provider 填 API Key，或在 `settings.json` 的
`providers[].apiKey` 里填（也支持 `DEEPSEEK_API_KEY` / `OPENAI_API_KEY`）。
没有 key 时会退回 Echo 模型并在日志里警告。

## 设置页

把 Master 后端切到 “ULin Core” 后，设置弹窗自动换成 7 个页签：

- **提供商**：provider 列表 / 增删改 / 选中；
- **模型**：按 provider 管理模型（id、显示名、maxTokens、temperature），选中默认模型；
- **运行时**：系统提示词、工作目录、工具线程数、最大步数、是否允许 `write_file`；
- **数据**：settings 路径、数据目录、打开目录、重新读取、恢复默认；
- **Agent**：原 Agent 预设页（Master 后端切换 + 预设），从这里切回 DSH；
- **外观**：原客户端外观设置；
- **服务**：原客户端 Server 设置。

切回 DSH 恢复原来的 4 页。保存后 `UlinCoreSettings::saved` 触发
`UlinCoreMasterAgent::reloadSettings()`：当前回合取消，模型和线程池立即重建，下一轮生效。

- 模型页的“获取模型”会带当前 provider 的 Key 请求 `{baseUrl}/models`，
  把新 id 补进列表；已有模型的 `maxTokens` / `temperature` 保留。
- 切到 ulincore 后，输入框的模型 chip 换成 ULin Core 的 provider/model：
  点开可直接选模型；未配 Key 时 chip 显示“未配置 Key”，弹层提示会退回 Echo。

## 工具线程池

`UlinCoreKernel` 持有一个 `QThreadPool`（`Options::toolThreads`，默认 4，钳在 1..16）。
工具在池线程执行；`toolStarted` / `toolFinished`、消息账本和 `startStep()` 都用
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回到内核线程，GUI 不会被文件/搜索阻塞。
`cancel()` 会 `clear()` 排队任务；正在跑的工具无法中断，回包会因代次号失效被丢弃。
内置工具都是无状态的（局部 QFile/QDir），可安全并发。

## 当前限制 / 下一步

- 工具在线程池执行；正在跑的工具无法中断，取消只丢弃排队任务与回包。
- 没有审批 UI；`write_file` 目前只靠 `allowWrites` 硬开关，下一步接「每次写/命令弹确认」。
- 没有 prompt 模板/压缩；消息账本原样进模型，长会话需要摘要或截断策略。
- 消息历史已落盘并会在 `session/follow` 回放；`session/page` 还没做分页（只回空）。
- 模型请求没有重试/退避；网络错误会以 `assistant/message` + `turn/end` 收尾。
- Journal 只写不读（`readAll` 已留），下一步做投影/回放。
