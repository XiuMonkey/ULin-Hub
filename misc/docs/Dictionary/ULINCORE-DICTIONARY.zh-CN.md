# ULinCore 名词字典

> 用途：统一 ULinCore 术语，区分容易混淆的概念。
> 原则：一个词只表达一个概念；与代码冲突时，先修正本字典或代码，不允许两套说法并存。

---

## 1. 核心对象

### Intent

- 英文：Intent
- 定义：一个可执行能力的声明。
- 组成：`name`、`description`、`parameters`、`prompt`、`skill`、`steps`、`executor`。
- 边界：Intent 是“能力声明”，不是一次调用，也不是执行结果。

### IntentCall

- 英文：IntentCall
- 定义：一次“调用 Intent”的请求。
- 组成：`name`、`arguments`、`metadata`。
- 不要称为：ToolCall。
- 边界：IntentCall 只描述“调用什么、参数是什么”，不描述执行结果。

### IntentResult

- 英文：IntentResult
- 定义：一次 Intent 调用的结果。
- 组成：成功/失败、`content`、`data`、`error`。
- 不要称为：ToolResult。
- 边界：`IntentResult` 是 call 的结果，不等于 Native execute 的原始结果。

### ExecuteResult

- 英文：ExecuteResult
- 定义：`NativeExecutor::execute()` 的原始执行结果。
- 组成：`ok`、`content`、`data`、`error`。
- 边界：`ExecuteResult` 只属于 execute 层；`IntentCaller` 会把它转换成 `IntentResult`。

### Native Intent

- 中文：原生 Intent
- 定义：`executor` 非空的 Intent。
- 执行：`IntentCaller` 查 `NativeRegistry`，调用对应 `NativeExecutor`。
- 边界：Native Intent 仍然由 Intent 声明，不直接暴露给 Agent 调 executor。

### Prompt Intent

- 中文：提示词 Intent
- 定义：没有 Native/Chain，主要靠 `prompt` 执行的 Intent。
- 执行：展开 `prompt` 后交给模型。
- 边界：Prompt Intent 可以被用户或 Agent 调用；不递归起额外模型回合。

### Chain Intent

- 中文：调用链 Intent
- 定义：`steps` 非空的 Intent。
- 执行：按顺序调用其他 Intent，可递归。
- 边界：`steps` 只保存 `intent` 和 `arguments`，不保存调用 id/result/日志。

### IntentStore

- 英文：IntentStore
- 定义：Intent 的内存集合与持久化入口。
- 行为：加载、保存、按名字查找、列出全部 Intent。
- 边界：不执行 Intent，不解析 `/it`。

### IntentCaller

- 英文：IntentCaller
- 定义：执行一次 Intent 的调度器。
- 行为：Native 分支走 executor；Chain 分支递归 call；不继承 NativeExecutor。
- 边界：只负责 call Intent，不负责解析输入、不负责模型协议。

### NativeExecutor

- 英文：NativeExecutor
- 定义：原生功能执行接口。
- 行为：`execute(arguments, context)` 返回 `ExecuteResult`。
- 边界：不解析 `/it`，不解析 Intent 名称，不负责参数 schema。

### NativeRegistry

- 英文：NativeRegistry
- 定义：NativeExecutor 的注册表。
- 键：完整 executor ID，例如 `builtin.read_file`。
- 边界：只按完整 ID 查找；不做去前缀回退。

### ExecutorContext

- 英文：ExecutorContext
- 定义：原生执行器运行时环境。
- 字段：`workspaceRoot`、`sessionId`、`turnId`、`allowWrites`、`allowShell`。
- 边界：不是 Intent 参数，用户不能通过 `/it` 参数修改。

---

## 2. 调用与执行

### call

- 中文：调用
- 定义：调用一个 Intent。
- 入口：`IntentCaller::call`。
- 结果：`IntentResult`。
- 不要与 execute 混用。

### execute

- 中文：执行
- 定义：执行 NativeExecutor 的原生功能。
- 入口：`NativeExecutor::execute`。
- 结果：`ExecuteResult`。
- 不要与 call 混用。

### 参数

- 英文：arguments
- 定义：一次调用实际传入的命名值，类型为 `QJsonObject`。
- 边界：arguments 是运行时数据；parameters 是声明。

### 参数声明

- 英文：parameters
- 定义：Intent 声明自己接受的参数名、类型、默认值。
- 边界：parameters 是 schema；executor 仍按 key 自己读取 arguments。

### 参数绑定

- 中文：binding
- 定义：按 Intent.parameters 对 arguments 做默认值填充和类型转换。
- 边界：不做字段重命名；executor 读取的 key 必须与声明一致。

### `$param`

- 定义：Chain step 中引用父调用参数的占位符。
- 示例：`{ "$param": "path" }`。
- 边界：只在 steps.arguments 中解析。

---

## 3. 输入与类型

### `/it`

- 定义：ULinCore 的用户输入语法入口。
- 形式：`/it <name> {arguments} -> {metadata} <tail>`。
- 边界：`/it` 由 ULinCore 解析，不由 QmlBridge 或 DSH 命令层解析。

### UlinCoreInvocation

- 定义：`/it` 二次解析后的调用描述。
- 组成：`kind`、`mode`、`intentName`、`arguments`、`tags`、`definition`、`prompt/skill override`。
- 边界：Invocation 是解析结果，不是 Intent 本身。

### IntentMode

- 定义：一次调用的语义类型。
- 取值：

```text
Definition   定义型
Generation   生成型
Execution    执行型
```

- 判定：

```text
有 Intent 元数据              -> Definition
无 Intent + 无同名 Intent     -> Generation
无 Intent + 有同名 Intent     -> Execution
```

- 边界：普通文本是 Prompt，不进入 IntentMode 判定。

### 定义型

- 英文：Definition
- 定义：本次调用携带完整 `Intent` 元数据。
- 默认：定义并执行；`defineOnly` 只定义。
- 边界：定义型不允许顶层 `prompt` / `skill` 覆盖。

### 生成型

- 英文：Generation
- 定义：无 `Intent` 元数据、无同名 Intent，由元数据 + Agent 行为链生成 Intent。
- 来源：`prompt`、`skill`、Agent 的 IntentCall 行为链、可选的 Agent 参与。
- 边界：生成完成前不能作为执行型使用。

### 执行型

- 英文：Execution
- 定义：无 `Intent` 元数据，但同名 Intent 已存在。
- 行为：调用已有 Intent；可用 `asPrompt` / `asSkill` / `withSkill`。
- 边界：不能同时定义新 Intent。
- 判定注意：`无 Intent 元数据`只是必要条件，不充分。  
  还必须满足“同名 Intent 已存在”；否则是生成型。  
  普通文本也没有 Intent 元数据，但它是 Prompt，不是执行型。
- 与定义型的区别：定义型携带 `Intent` 元数据，默认可以 `defineAndRun`；  
  即使它执行了 Intent，本次调用的语义类型仍是定义型。

---

## 4. 元数据与标签

### metadata

- 中文：元数据
- 定义：调用期附加信息，不属于 Intent 持久化字段。
- 键：`prompt`、`skill`、`Intent`、`tags`。
- 边界：metadata 不写进 `intents.json`，除非它被用于定义/生成 Intent。

### tags

- 中文：标签
- 定义：调用期布尔状态数组。
- 边界：tags 只写偏离默认的状态；不是 Intent 字段。

### asPrompt

- 定义：执行型标签；只使用 Intent 内置 `prompt`。
- 禁止：外部 `prompt`；单独使用时禁止外部 `skill`。
- 例外：与 `withSkill` 组合时允许外部 `skill` 覆盖。

### asSkill

- 定义：执行型标签；必须使用外部 `prompt`。
- skill：外部 `skill` 优先，否则使用 `Intent.skill`。
- 禁止：回退到 `Intent.prompt`。

### withSkill

- 定义：执行型标签；使用 Intent 内置 `prompt` + `skill`。
- 允许：外部 `skill` 覆盖内置 `skill`。
- 禁止：外部 `prompt`。
- 约束：Native/Chain Intent 使用时必须叠加 `asPrompt`；Prompt-only 可省略。

### skillRequired

- 定义：生成型标签；要求 Agent 生成 `skill`。
- 必需：`prompt` + `agentEngaged`。
- 禁止：外部 `skill`。
- 边界：属于生成型，不能与执行型标签混用。

### agentEngaged

- 定义：生成型标签；允许 Agent 制定 Intent，不执行 seed。
- 必需：`prompt` + `save`。
- 边界：不是执行型标签，不代表“执行后泛化”。

### generateOnly

- 定义：生成型标签；只生成 Intent，不执行 seed。
- 必需：`prompt` + `save`。
- 边界：可以与 `agentEngaged` 共存。

### save

- 定义：持久化控制标签。
- 规则：

```text
定义并执行       可选
默认生成         可选
defineOnly       必需
generateOnly     必需
agentEngaged     必需
执行已有 Intent  禁止
```

- 边界：`save` 不决定 IntentMode，只决定是否落盘。

### defineOnly

- 定义：定义型标签；只定义，不执行。
- 必需：`Intent` + `save`。
- 边界：不能与执行型/生成型标签混用。

---

## 5. 模型与协议

### wire name

- 中文：线上名称
- 定义：发给模型的合法 function name。
- 规则：合法名称原样保留；包含 `.` 等非法字符时编码为 `intent_<base64url>`。
- 边界：wire name 只存在于模型边界，Kernel 内部仍使用原 Intent 名。

### `__ulincore_meta`

- 定义：模型工具 arguments 中的 ULin 保留键。
- 作用：承载 `tags`、`Intent` 等 metadata。
- 边界：只在模型协议中出现，Kernel 内部使用 `IntentCall.metadata`。

### Model

- 中文：模型
- 定义：实际生成文本/工具调用的 LLM。
- 边界：Model 不是 Agent；Agent 是包含模型、工具循环和状态机的执行体。

### Agent

- 中文：代理
- 定义：能调用 Intent、使用工具、维护回合的智能执行体。
- 边界：Agent 只能调用定义型/执行型；不能调用生成型。

### Turn

- 中文：回合
- 定义：一次用户输入到结束的完整执行周期。
- 边界：一轮可以包含多次 IntentCall。

### Session

- 中文：会话
- 定义：连续多轮对话及其事件账本。
- 边界：Session 不等于单次 Turn。

### Journal

- 中文：会话日志
- 定义：追加式事件记录。
- 边界：Journal 不是模型上下文，也不是 IntentStore。

---

## 6. 模块与传输

### ULinCoreKernel

- 定义：ULinCore 内核；负责解析后的分派、模型循环、Intent 执行、回合状态。
- 边界：不处理 QML，不处理 DSH 命令，不直接面向 UI。

### ULinCoreMasterAgent

- 定义：ULinCore 作为 Master Agent 的 DSH 协议适配层。
- 职责：接收 `session/prompt`、调用 Kernel、把内核事件翻成 session 事件。
- 边界：不是内核本身。

### DSH

- 定义：宿主使用的本地 RPC/会话服务。
- 边界：ULinCore 模式下属传输网关；不是当前 Agent。

### QmlBridge

- 定义：QML 到 C++ 的接线层。
- 边界：不解析 `/it`，不判断 DSH 命令，不发 RPC。

### ChatController

- 定义：前端会话控制器；维护消息模型、附件、事件、错误、raw 发送。
- 边界：不解析 DSH 命令，不解析 `/it`。

### DshApiClient

- 定义：前端到本地 DSH 服务的 JSON-RPC/mux 客户端。
- 边界：只做传输，不做语义判断。

### DshPromptClient

- 定义：DSH 专属输入客户端；命令走 `commands/execute`，普通文本走 `session/prompt`。
- 边界：不处理 `/it`；ULinCore 不使用它接管输入。

### Takeover

- 中文：接管
- 定义：把 `agent.prompt` 连接的接收者从默认 `sendRawPrompt` 切换为 DSH 客户端。
- 边界：只切换接收者，不改变输入语法。

---

## 7. 禁止/废弃术语

### ToolCall

- 状态：废弃。
- 原因：核心只有 IntentCall；ToolCall 是旧模型/旧架构词汇。
- 替代：IntentCall。

### ToolResult

- 状态：废弃。
- 原因：核心只有 IntentResult / ExecuteResult。
- 替代：IntentResult（call 结果）或 ExecuteResult（native 结果）。

### Recipe

- 状态：不推荐。
- 原因：容易被理解成独立于 Intent 的第二种能力。
- 替代：Chain Intent / steps。

### 命令

- 状态：仅用于 DSH 的 `/command`。
- 边界：ULinCore 的 `/it` 不是 DSH 命令；不要混称。
