# ULinCore 旧文档碎片（临时）

> 状态：临时提取，不是规范，不进入字典。
> 用途：旧文档归档前，保留可能仍有参考价值、但尚未合并进 `Dictionary/ULINCORE-DICTIONARY.zh-CN.md` 的碎片。
> 合并或确认无用后删除本文件。

---

## 1. 目录职责

```text
types/      数据结构
intent/     声明、解析、绑定、存储、IntentCaller
executor/   NativeExecutor、NativeImplementation、内置实现
model/      模型适配
model/protocol/  wire metadata 映射
core/       Kernel、MasterAgent、Session、Journal
settings/   设置
```

边界：

```text
Intent 声明能力
IntentCaller 调 Intent
NativeExecutor 执行原生功能
Model 只负责模型协议
MasterAgent 只负责 DSH 协议适配
```

---

## 2. call / execute 历史边界

```text
call     = 调用 Intent
execute  = 执行 Native 实现
```

```text
IntentCaller.call       -> IntentResult
NativeExecutor.execute  -> ExecuteResult
```

约束：

```text
IntentCaller 不继承 NativeExecutor
Agent 不能直接调用 NativeExecutor
Native 只对内置 Intent 暴露
```

---

## 3. 输入链路要点

```text
Composer
  ↓
QmlBridge
  ↓
ChatController
  ↓
ConnectionManager: agent.prompt
  ├─ DSH: DshPromptClient -> commands/execute / session/prompt
  └─ 默认: ChatController::sendRawPrompt -> session/prompt
  ↓
ULinCoreMasterAgent
  ↓
UlinCoreKernel
  ↓
UlinCoreParser
```

边界：

```text
QmlBridge 不解析 /it
ChatController 不解析 DSH 命令
DshApiClient 只做 RPC/mux
DshPromptClient 只处理 DSH 命令
ULinCore 不处理 DSH 命令
```

---

## 4. 模型 wire 细节

```text
IntentCall.metadata
  ↔ arguments.__ulincore_meta
```

- 只在模型边界存在。
- Kernel 内部不泄漏 `__ulincore_meta`。
- 非法 function name 使用 `intent_<base64url>`；合法名字原样保留。
- 工具 schema、assistant 历史、模型返回三处都要编解码。

---

## 5. Intent 持久化

```text
data/ulincore/intents.json
```

- 严格 JSON 数组。
- `QSaveFile` 原子写。
- 按 name 排序。
- 加载后重新安装内置 Intent。
- 内置与用户同名时，内置优先。

当前缺失：

```text
版本
来源
创建/更新时间
修改历史
删除/禁用
文件锁
并发写保护
保存失败回滚
```

---

## 6. 安全上下文

```cpp
ExecutorContext
  workspaceRoot
  sessionId
  turnId
  allowWrites
  allowShell
```

- 来自内核/宿主配置，不是 Intent 参数。
- 用户不能通过 `/it` 参数修改。
- `write_file` 等执行器依赖 `allowWrites`。

---

## 7. 旧文档中的示例

Prompt Intent：

```text
/it demo.explain {topic:"QML"} -> {
  prompt:"解释 [topic]。",
  skill:"先给定义，再给例子。",
  tags:["save"]
}
```

Chain Intent：

```text
/it demo.inspect_workspace {path:"."} -> {
  Intent:{
    "description":"列出目录并搜索项目入口",
    "parameters":[{"name":"path","type":"string"}],
    "steps":[
      {"intent":"list_dir","arguments":{"path":{"$param":"path"}}},
      {"intent":"search_text","arguments":{
        "path":{"$param":"path"},
        "query":"CMakeLists",
        "maxResults":10
      }}
    ]
  },
  tags:["defineOnly","save"]
}
```

Native Intent：

```json
{
  "name": "example.native",
  "executor": "builtin.read_file"
}
```

---

## 8. 待确认后再合并的点

```text
Prompt-only Intent 的工具表暴露细节
生成型 steps 的参数快照语义
skillRequired 的模型生成提示词
Intent 版本/删除/锁设计
命令 UI 扩展迁移
```
