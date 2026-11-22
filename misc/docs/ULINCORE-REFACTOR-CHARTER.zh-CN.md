# ULinCore / Agent 输入链路重构约束

> 目的：固定当前事实与已确定决策，防止后续修改反复漂移。
> 本文件优先于对话中的临时方案；与代码冲突时，以本文件的“目标”标注为准，并更新“当前状态”。

---

## 1. 范围

本次关注：

```text
用户输入
  ↓
QML
  ↓
C++ 前端
  ↓
Agent 后端
```

明确不在本次范围：

```text
扩展 UI 组件迁移
扩展 API / ABI
命令菜单抽成扩展组件
DSH 服务端 Node 包改造
```

---

## 2. 层级职责

### QML

```text
Composer.qml
  输入框、键盘、附件、界面交互
```

QML 不解析：

```text
/it
DSH 命令
ULinCore 语法
```

### QmlBridge

```text
QML → C++ 的接线层
```

只负责：

```text
把原始输入交给 ChatController
```

不负责：

```text
命令解析
/it 特判
masterBackend 分支
session/prompt
commands/execute
```

### ChatController

目标职责：

```text
当前会话状态
用户消息/附件
事件接收与模型更新
错误展示
调用当前 AgentPromptClient
```

不负责：

```text
DSH 命令判断
/it 解析
直接发 session/prompt
直接发 commands/execute
```

> 当前状态：ChatController 仍直接发 session/prompt，AgentPromptClient 只做前置拦截。这是过渡实现，不是目标。

### AgentPromptClient

目标职责：

```text
当前 Agent 的输入入口
决定命令 / prompt
完成对应 RPC
把异步结果通过信号槽回给 ChatController
```

必须是：

```text
QObject
信号：accepted / failed
不传 std::function 回调
```

### DSH 客户端

```text
命令 -> commands/execute
普通文本 -> session/prompt
```

DSH 命令逻辑只能在 DSH 模块内。

### ULinCore 客户端

```text
全部文本 -> session/prompt
```

`/it` 由 ULinCoreKernel 的二次解析处理。

ULinCore 不实现命令解析。

### DshApiClient

```text
只做 JSON-RPC 传输
```

不判断命令，不解析 `/it`。

### DSH 服务端

```text
commands/execute：命令运行时
session/prompt：Agent Loop / 模型
```

当前 DSH 不会在 `session/prompt` 中自动解析命令。  
因此 DSH 命令必须在 DSH 客户端或 DSH 服务端前置处理。

---

## 3. 核心不变量

### 3.1 Intent 是唯一能力声明

```text
Intent
IntentCall
IntentResult
ExecuteResult
```

核心层不再使用：

```text
ToolCall
ToolResult
Role::Tool
toolCalls
toolCallId
```

旧的 wire 字段只允许出现在模型协议适配层，并有 fallback 读取。

### 3.2 call 与 execute

```text
call     = 调用 Intent
execute  = 执行原生功能
```

```text
IntentCaller.call       -> IntentResult
NativeExecutor.execute  -> ExecuteResult
```

`IntentCaller` 不继承 `NativeExecutor`。

### 3.3 原生功能只对内置 Intent 暴露

```text
Agent -> call Intent
Native Intent -> execute Native
```

Agent 不能直接调用 NativeExecutor。

### 3.4 模型协议只做边界映射

```text
IntentCall.metadata
        ↕
arguments.__ulincore_meta
```

协议适配只允许在 `model/protocol/` 中。

### 3.5 禁止前端解析输入语义

禁止在以下位置出现：

```text
QmlBridge 里判断 /it
QmlBridge 里判断 DSH 命令
ChatController 里判断 DSH 命令
ULinCore 里判断 DSH 命令
```

### 3.6 异步结果用信号槽

禁止新增：

```cpp
std::function<void()> onAccepted
std::function<void(const QString&)> onError
```

用于 QObject 生命周期内的异步 RPC。  
改用：

```cpp
signals:
    void accepted();
    void failed(const QString&);
```

虚函数只用于同步入口多态，不用于异步回调传递。

---

## 4. 当前实现快照

截至本文件创建时：

```text
tool/ 已并入 executor/
IntentCall / IntentResult / ExecuteResult 已拆分
UlinCoreIntentCaller 已移入 intent/
NativeExecutor / NativeImplementation 已命名
model/protocol 已下沉
Composer 已加 commandButton / commandPanel 插槽
DshPromptClient 已存在
AgentPromptClient 当前是 bool submit 接口
QmlBridge 仍按 masterBackend 选择 client
CommandController 仍在 common/session
命令菜单仍内置在 Composer
```

这些是过渡状态，不代表目标完成。

---

## 5. 已确定决策

| 日期 | 决策 |
|---|---|
| 2026-11-22 | `ToolCall` → `IntentCall`，`ToolResult` → `IntentResult` |
| 2026-11-22 | 原生结果独立为 `ExecuteResult` |
| 2026-11-22 | `call Intent` 与 `execute Native` 分离 |
| 2026-11-22 | `protocol` 移到 `model/protocol` |
| 2026-11-22 | `/it` 不由 QmlBridge 特判 |
| 2026-11-22 | DSH 命令由 DSH 侧处理 |
| 2026-11-22 | 暂不提供扩展 Input 接口 |
| 2026-11-22 | 暂不做命令 UI 扩展迁移 |

---

## 6. 未决问题

### 6.1 DSH 命令位置

两个候选：

```text
A. DSH 前端客户端
   DshPromptClient -> commands/execute / session/prompt

B. DSH 服务端
   session/prompt -> 内部命令分流
```

当前代码是 A。  
若选 B，需要改 DSH Node 服务端，C++ 前端只发原始 prompt。

### 6.2 ChatController 的最终职责

目标：

```text
ChatController 交接输入后返回
RPC 由 AgentPromptClient 完成
```

当前：

```text
ChatController 仍发 session/prompt
```

迁移时必须同时处理：

```text
用户消息何时 append
命令是否显示
附件准备
发送失败
重启服务端
补发
```

不能只搬 `callMethod`。

### 6.3 AgentPromptClient 形态

当前：

```cpp
virtual bool submit(sessionId, text) = 0;
```

目标：

```cpp
class AgentPromptClient : public QObject
{
    Q_OBJECT
public:
    virtual void submit(const PromptRequest& request) = 0;
signals:
    void accepted();
    void failed(const QString&);
};
```

改造前必须确认 `PromptRequest` 的元类型和线程模型。

### 6.4 扩展命令 UI

已决定暂不做。

已加插槽：

```text
composer.commandButton
composer.commandPanel
```

但不应在本次继续迁移命令菜单。

---

## 7. 禁止事项

```text
不要把 ToolCall/ToolResult 加回核心
不要让 QmlBridge 解析 /it
不要让 ChatController 解析 DSH 命令
不要让 ULinCore 认识 DSH commands
不要把 DSH 命令逻辑放进 DshApiClient
不要用 std::function 传异步 RPC 结果
不要在没有决策记录的情况下新增通用 Router/Input 接口
不要在本轮迁移命令 UI 扩展
```

---

## 8. 修改前检查表

每次改输入链路前先确认：

```text
[ ] 改的是 QML / QmlBridge / ChatController / AgentClient 哪一层？
[ ] 是否让前端知道了 /it 或 DSH 命令？
[ ] 是否让 ChatController 直接调 commands/execute？
[ ] 是否新增了不必要的虚函数/回调？
[ ] 异步结果是否走信号槽？
[ ] ULinCore 是否仍然只收到原始文本？
[ ] DSH 命令是否仍然只由 DSH 侧处理？
[ ] 是否影响附件/错误/重试/补发？
```

---

## 9. 目标链路

### DSH 命令

```text
Composer
  ↓
QmlBridge
  ↓
ChatController.submit()
  ↓
DshPromptClient.submit()
  ↓
commands/execute
  ↓
command/done
  ↓
ChatController 事件处理
```

### DSH 普通文本

```text
Composer
  ↓
QmlBridge
  ↓
ChatController.submit()
  ↓
DshPromptClient.submit()
  ↓ 不是命令
session/prompt
  ↓
DSH Agent Loop
```

### ULinCore

```text
Composer
  ↓
QmlBridge
  ↓
ChatController.submit()
  ↓
ULinCorePromptClient.submit()
  ↓
session/prompt
  ↓
ULinCoreMasterAgent
  ↓
UlinCoreKernel
  ↓
UlinCoreParser
```

---

## 10. 审查原则

```text
先看本文件，再看代码。
代码与本文件冲突时，不得默默改代码；
必须先更新“当前状态”或“决策”，再实施。
```

这份文档是约束，不是愿望清单。  
任何新抽象、新接口、新目录，都必须先在这里留下理由。
