# ULinCore 目录与文件说明

> 路径基准：`source/{include,src}/shared/common/agent/ulincore/`

## 1. 总览

```text
ulincore/
├── types/       基础数据类型
├── intent/      Intent 声明、解析、绑定、持久化
├── executor/    执行器、内置 Intent、Intent 执行
├── protocol/    模型边界 metadata 协议适配
├── core/        内核状态机、DSH 适配、会话、Journal
├── model/       模型适配
└── settings/    设置
```

```text
types 是数据
intent 是声明与解析
executor 是执行
protocol 是模型线上的格式适配
core 是编排
model 是模型通信
settings 是配置
```

---

## 2. types/

```text
types/
└── UlinCoreTypes.h/.cpp
```

| 内容 | 作用 |
|---|---|
| `Role` | user / assistant / tool / system 等角色 |
| `Message` | 会话消息 |
| `ToolCall` | 模型发起的工具调用 |
| `ToolResult` | 工具执行结果 |
| `Usage` | token 用量 |
| `TurnResult` | 一轮结束结果 |

`ToolCall` 当前结构：

```cpp
struct ToolCall
{
    QString id;
    QString name;
    QJsonObject arguments;
    QJsonObject metadata;   // tags / Intent
};
```

`ToolCall` 是模型侧调用记录，不等同于 Intent，也不等同于 Executor。

---

## 3. intent/

```text
intent/
├── UlinCoreIntent.h/.cpp
├── UlinCoreInvocation.h
├── UlinCoreParser.h/.cpp
├── UlinCoreBinding.h/.cpp
└── UlinCoreIntentStore.h/.cpp
```

### 3.1 UlinCoreIntent

统一 Intent 数据模型：

```cpp
struct Intent
{
    QString name;
    QString description;
    QString prompt;      // 用户意图
    QString skill;       // 泛化方法
    QVector<IntentParameter> parameters;
    QVector<IntentStep> steps;
    QString executor;    // 原生执行器 ID
};
```

能力判断：

```text
hasPrompt()
hasSkill()
hasChain()
hasNative()
defaultPromptMode()
```

以及：

```text
IntentParameter  参数声明
IntentStep       对另一个 Intent 的调用
```

### 3.2 UlinCoreInvocation

`/it` 解析后的调用对象：

```text
kind
rawText
intentName
promptText
args
tags
hasDefinition
definition
execute
promptOverride
hasPromptOverride
error
```

标签辅助：

```text
hasTag()
asPrompt()
asSkill()
save()
agentEngaged()
skillRequired()
defineOnly()
```

### 3.3 UlinCoreParser

负责把文本解析成 `UlinCoreInvocation`：

```text
/it name {args} -> {metadata} tail
```

主要工作：

- 参数块 `{key:value}`；
- 元数据块；
- `tags` 数组；
- `Intent:{...}` 定义块；
- 标签冲突、标签与键值对冲突；
- 不查 Intent 表，不执行。

### 3.4 UlinCoreBinding

参数和 Prompt 绑定：

- `$param` 解析；
- `[name]` / `{{name}}` 模板展开；
- 参数 schema 生成；
- 参数类型推断与强转。

### 3.5 UlinCoreIntentStore

Intent 持久化：

```text
data/ulincore/intents.json
```

接口：

```text
load / save
add / find / contains / all
catalogText
```

---

## 4. executor/

```text
executor/
├── UlinCoreExecutor.h/.cpp
├── UlinCoreIntentExecutor.h/.cpp
├── UlinCoreBuiltinIntents.h/.cpp
├── UlinCoreBuiltinExecutors.h/.cpp
├── UlinCoreExecutorBackend.h/.cpp
└── UlinCoreBuiltinExecutorBackends.h/.cpp
```

### 4.1 UlinCoreExecutor

执行器契约：

```cpp
struct ExecutorContext
{
    QString workspaceRoot;
    QString sessionId;
    QString turnId;
    bool allowWrites;
    bool allowShell;
};

class UlinCoreExecutor
{
    virtual ToolResult execute(
        const QJsonObject& arguments,
        const ExecutorContext& context) = 0;
};
```

以及：

```text
UlinCoreExecutorRegistry
```

按 ID 注册：

```text
builtin.read_file
builtin.list_dir
builtin.write_file
builtin.search_text
```

### 4.2 UlinCoreIntentExecutor

执行一棵 Intent：

```text
Native Intent -> ExecutorRegistry
Chain Intent  -> 递归执行 steps[].intent
```

带递归深度和自引用保护。

### 4.3 UlinCoreBuiltinIntents

硬编码内置 Intent 声明：

```text
read_file
list_dir
write_file
search_text
```

每个包含：

```text
name
description
parameters
executor
```

不包含执行代码。

### 4.4 UlinCoreBuiltinExecutors

把内置执行器注册到 `ExecutorRegistry`。

### 4.5 UlinCoreExecutorBackend

内置执行器的底层实现接口和注册表。

当前仍保留：

```text
name
description
parametersSchema
execute
```

它是执行后端，不是 Intent。

### 4.6 UlinCoreBuiltinExecutorBackends

四个内置执行器的具体实现：

```text
ReadFile
ListDir
WriteFile
SearchText
```

---

## 5. protocol/

```text
protocol/
└── UlinCoreModelProtocol.h/.cpp
```

只负责模型边界的线上格式适配，不参与 Kernel 架构。

映射关系：

```text
ToolCall.metadata
        ↕
arguments.__ulincore_meta
```

发给模型：

1. 给每个工具 schema 注入可选 `__ulincore_meta`；
2. 把 `ToolCall.metadata` 编进 arguments。

模型返回：

1. 从 arguments 取出 `__ulincore_meta`；
2. 还原成 `ToolCall.metadata`；
3. 从真实参数中删除保留键。

API：

```cpp
encodeToolSchemas()
encodeToolArguments()
decodeToolArguments()
metadataSchema()
```

---

## 6. core/

```text
core/
├── UlinCoreKernel.h/.cpp
├── UlinCoreMasterAgent.h/.cpp
├── UlinCoreSession.h/.cpp
└── UlinCoreJournal.h/.cpp
```

### 6.1 UlinCoreKernel

Agent 状态机：

- 调 Parser；
- 查 IntentStore；
- 分派 Prompt / Native / Chain / Define；
- 调模型；
- 处理 tool calls；
- 控制步数、取消、代次；
- 统计用量；
- 发回合和工具事件；
- 保存 Intent。

### 6.2 UlinCoreMasterAgent

DSH 适配层：

- 注册 Master Agent；
- DSH 出站事件；
- 会话/回合事件转换；
- 把 ULinCore 事件转发给 DSH；
- 加载 Settings。

### 6.3 UlinCoreSession

一次会话的消息账本：

- 用户/助手/工具消息；
- snapshot / restore；
- 会话持久化辅助。

### 6.4 UlinCoreJournal

JSONL 事件账本：

```text
turn/start
tool/call
tool/result
assistant/message
turn/end
```

用于审计和回放。

---

## 7. model/

```text
model/
├── UlinCoreModel.h
├── UlinCoreEchoModel.h/.cpp
└── UlinCoreOpenAiModel.h/.cpp
```

### 7.1 UlinCoreModel

模型接口：

- 流式增量；
- 最终 tool calls；
- 用量；
- cancel。

### 7.2 UlinCoreEchoModel

无网络确定性模型，用于测试和没有 API Key 时的回退。

### 7.3 UlinCoreOpenAiModel

OpenAI 兼容模型：

- `/chat/completions`；
- SSE；
- tool calls；
- 用量；
- metadata 协议编解码。

---

## 8. settings/

```text
settings/
└── UlinCoreSettings.h/.cpp
```

读写：

```text
data/ulincore/settings.json
```

内容：

- provider / model；
- systemPrompt；
- workspaceRoot；
- toolThreads / maxSteps；
- allowWrites；
- 运行状态。

---

## 9. 调用链

### 9.1 用户入口

```text
/it ...
  ↓
UlinCoreParser
  ↓
UlinCoreInvocation
  ↓
UlinCoreKernel
  ↓
IntentStore
  ├── Native  -> ExecutorRegistry
  ├── Chain   -> IntentExecutor -> steps
  ├── Prompt  -> Model
  └── Define  -> IntentStore
```

### 9.2 Agent 入口

```text
model tool call
  ↓
Protocol::decodeToolArguments
  ↓
ToolCall.metadata
  ↓
UlinCoreKernel::executeTool
  ↓
IntentStore
  ↓
Executor / IntentExecutor
```

### 9.3 模型请求

```text
Kernel
  ↓
ModelRequest.tools
  ↓
Protocol::encodeToolSchemas
  ↓
OpenAI Model
```

---

## 10. 当前状态

### 已收敛

```text
Intent 统一模型
Executor 执行契约
IntentExecutor 组合执行
Protocol metadata 适配
tool/ 目录并入 executor/
```

### 过渡中

```text
UlinCoreExecutorBackend
UlinCoreExecutorBackendRegistry
UlinCoreBuiltinExecutorBackends
```

这些是内置能力的底层执行后端，命名和执行器语义已经分开，但仍保留旧 Tool 风格的方法。

### 尚未完成

- Agent metadata 的完整生产/消费闭环；
- `isAgentEngaged` / `skillRequired` 自动泛化；
- `Intent:{...}` 的 JSON5 支持；
- 旧后端适配层进一步合并。
