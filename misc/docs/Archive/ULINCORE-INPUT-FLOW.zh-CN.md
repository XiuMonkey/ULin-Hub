# ULinCore / DSH 输入链路速查

> 本文是输入链路的审查基准。改代码前先看这里；代码与本文冲突时，先更新本文的状态，不得默默改代码。

相关约束见：

```text
misc/docs/ULINCORE-REFACTOR-CHARTER.zh-CN.md
```

---

## 1. 目标总原则

```text
QmlBridge：只做 QML → C++ 接线
ChatController：会话状态、用户消息、事件、raw 发送
ConnectionManager：决定当前输入接收者
DshPromptClient：DSH 自己分流命令 / 普通文本
UlinCore：自己解析 /it
DshApiClient：只做 RPC 传输
```

禁止：

```text
QmlBridge 判断 /it
QmlBridge 判断 DSH 命令
ChatController 判断 DSH 命令
DshApiClient 判断命令
ULinCore 处理 DSH 命令
```

---

## 2. ULinCore 输入路径

| 步骤 | 调用 | 职责 | 结果 |
|---|---|---|---|
| 1 | `Composer.qml` | 收集输入框文本、附件、引用、键盘事件 | `Hub.sendPrompt(text)` |
| 2 | `QmlBridge::sendPrompt` | QML→C++ 接线，不解析 `/it` | 原始文本交给 ChatController |
| 3 | `ChatController::submitPrompt` | 保存会话和重启/状态回调 | 准备提交输入 |
| 4 | `emit promptRequested` | 广播输入请求，不直接发 RPC | 信号进入连接表 |
| 5 | `ConnectionManager` | ULinCore 不接管 `agent.prompt` | 使用默认接收者 |
| 6 | `ChatController::sendRawPrompt` | 检查模型、准备附件、追加用户消息、发送 | 进入普通发送 |
| 7 | `appendUser(text)` | `/it ...` 或普通文本显示为用户消息 | 用户消息上屏 |
| 8 | `DshApiClient` | 只做 JSON-RPC / mux 传输 | 发出 `session/prompt` |
| 9 | 本地 DSH 服务端 | 会话网关，不解析 `/it` | 路由给当前 Master Agent |
| 10 | `ULinCoreMasterAgent::OnOutboundRequest` | 接收 `session/prompt`，提取文本 | 调用 Kernel |
| 11 | `UlinCoreKernel::run(text)` | 内核入口，管理回合/步数/取消 | 进入二次解析 |
| 12 | `UlinCoreParser::parse(text)` | 解析 `/it` | Prompt / Intent / Error |

### 2.1 `/it` 分派

| 输入 | Parser 结果 | Kernel 行为 |
|---|---|---|
| 普通文本 | `InvocationKind::Prompt` | `runPromptText()` → 模型 |
| 已知 Intent | `InvocationKind::Intent` | 查 IntentStore |
| Native Intent | 有 `executor` | `IntentCaller::call` → `NativeExecutor::execute` |
| Chain Intent | 有 `steps` | `IntentCaller::call` → 递归 call |
| Prompt Intent | 有 `prompt` | 展开 Prompt → 模型 |
| `AsPrompt` | 强制 Prompt | 忽略 skill/steps/executor |
| `AsSkill` | prompt + skill | 模型拿到方法 + 意图 |
| `defineOnly` | 只定义 | `addIntent()` → `save()`，不执行 |
| 定义并执行 | 有 Intent 无 defineOnly | `addIntent()` → `IntentCaller::call` |
| 未知 Intent + Prompt 元数据 | 创建 PromptIntent | 可选 save 后执行 |
| 语法错误 | `InvocationKind::Error` | 返回错误 |

### 2.2 输出回路

| 步骤 | 调用 | 职责 |
|---|---|---|
| 1 | `UlinCoreKernel` | 产生 `assistant/chunk` / `assistant/message` / `tool/call` / `tool/result` / `turn/end` |
| 2 | `ULinCoreMasterAgent::forwardEvent` | 内核事件 → DSH session 事件 |
| 3 | `appendSessionEvent` | 写本地会话日志 |
| 4 | DSH 服务端 | `session/follow` / mux 推送 |
| 5 | `DshApiClient` | 接收事件 |
| 6 | `ChatController::onMuxFrame` | 按会话过滤 |
| 7 | `ChatController::applyStreamEvent` | 解析事件 |
| 8 | `MessageListModel` | 更新消息/过程/工具/结果 |
| 9 | `AgentBubble` | 最终渲染 |

---

## 3. DSH 命令路径

| 步骤 | 调用 | 职责 | 结果 |
|---|---|---|---|
| 1 | `Composer.qml` | 收集输入 | `Hub.sendPrompt(text)` |
| 2 | `QmlBridge::sendPrompt` | QML→C++ 接线 | 原始文本 |
| 3 | `ChatController::submitPrompt` | 保存会话/回调 | 发出 `promptRequested` |
| 4 | `ConnectionManager` | DSH 接管 `agent.prompt` | 进入 `DshPromptClient` |
| 5 | `DshPromptClient::handlePrompt` | 判断命令 / 普通文本 | 命令或 raw |
| 6 | `CommandController::looksLikeCommand` | 严格命令文法 | true/false |
| 7 | `CommandController::execute` | 构造并执行命令 | `commands/execute` |
| 8 | `DshApiClient::callMethodValue` | JSON-RPC 传输 | 服务端命令运行时 |
| 9 | DSH 服务端 | `parseCommand` → `command/run` → handler → `command/done` | 命令结果 |
| 10 | `ChatController::applyStreamEvent` | 收到 `command/done` | 系统提示 |
| 11 | `MessageListModel::appendSystem` | 渲染命令结果 | 显示 |

命令路径不会：

```text
appendUser
session/prompt
模型
```

### 3.1 命令错误

| 情况 | 回传 | 展示 |
|---|---|---|
| 成功 | `command/done` | `appendSystem(text)` |
| 未知命令 | `CommandController::executionFinished(false, ...)` | `DshPromptClient` → `ChatController::appendSystemNotice` |
| RPC 失败 | `executionFinished(false, error)` | 同上 |
| busy | `CommandController` 内部拒绝 | 按当前策略 |

---

## 4. DSH 普通文本路径

| 步骤 | 调用 | 职责 |
|---|---|---|
| 1 | `DshPromptClient::handlePrompt` | 判断不是命令 |
| 2 | `ChatController::sendRawPrompt` | 回到普通发送 |
| 3 | `appendUser` | 用户消息上屏 |
| 4 | `DshApiClient` | `session/prompt` |
| 5 | DSH Agent Loop | 模型对话 |
| 6 | 事件流 | `assistant/chunk` / `assistant/message` |
| 7 | `ChatController::applyStreamEvent` | 更新模型 |
| 8 | `MessageListModel` | 渲染 |

---

## 5. 模块职责

| 模块 | 职责 | 禁止 |
|---|---|---|
| `Composer.qml` | 输入框、键盘、附件、UI 状态 | 解析 `/it` / DSH 命令 |
| `QmlBridge` | QML→C++ 接线 | 判断 `/it` / DSH 命令 |
| `ChatController` | 会话状态、用户消息、事件、错误、raw 发送 | 判断 DSH 命令 |
| `ConnectionManager` | 接管/恢复 `agent.prompt` | 解析输入 |
| `DshPromptClient` | DSH 命令/普通文本分流、错误回流 | 处理 `/it` |
| `CommandController` | DSH 命令发现/执行/结果 | 普通 Prompt |
| `DshApiClient` | JSON-RPC / mux 传输 | 语义判断 |
| DSH 服务端 | 命令运行时 / Agent Loop | 处理 `/it` |
| `ULinCoreMasterAgent` | 接收 `session/prompt` | 处理 DSH 命令 |
| `UlinCoreKernel` | Intent 分派 | 处理 DSH 命令 |
| `UlinCoreParser` | `/it` 二次解析 | 处理 DSH 命令 |

---

## 6. 连接接管

| 状态 | `agent.prompt` 接收者 |
|---|---|
| ULinCore / 默认 | `ChatController::sendRawPrompt` |
| DSH 激活 | `DshPromptClient::handlePrompt` |
| DSH 停用 | `ReleaseTakeover` → 默认 |

默认连接：

```text
ChatController::promptRequested
  -> ChatController::sendRawPrompt
```

DSH 接管：

```text
TakeoverConnection("agent.prompt", DshPromptClient, "handlePrompt")
```

停止：

```text
ReleaseTakeover("agent.prompt")
```

---

## 7. 当前代码状态

当前仍是过渡实现：

```text
AgentPromptClient 虚基类存在
DshPromptClient 仍是 bool submit
ChatController 仍直接发 session/prompt
QmlBridge 仍按 masterBackend 选择 client
还没有 promptRequested / TakeoverConnection
```

目标改造：

```text
1. 删除 AgentPromptClient
2. DshPromptClient 改 QObject + handlePrompt
3. ChatController 拆 submitPrompt / sendRawPrompt
4. 默认连接走 dshRegister("agent.prompt", ...)
5. DSH 激活 TakeoverConnection，停用 ReleaseTakeover
6. 删除 m_promptClient
```

---

## 8. 验收标准

```text
[ ] QmlBridge 没有 /it 判断
[ ] QmlBridge 没有命令判断
[ ] ChatController 没有 DSH 命令判断
[ ] /it 作为用户消息进入 ULinCore
[ ] DSH 命令不进入模型历史
[ ] DSH 命令结果显示为系统行
[ ] ULinCore 不处理 DSH 命令
[ ] 普通 DSH 文本仍走 session/prompt
[ ] 连接切换使用 ConnectionManager
[ ] 异步错误通过信号槽回流
```
