# ULinCore Intent 架构详解

> 路径基准：`source/{include,src}/shared/common/agent/ulincore/`

## 1. 设计目标

ULinCore 的核心目标不是“做一个工具调用器”，而是：

```text
把 Agent 的能力统一成 Intent。
```

所有能力都应该是 Intent：

```text
Prompt       自然语言能力
Skill        Prompt 的方法说明
Chain        多个 Intent 的组合
Native       绑定原生执行器的原子能力
```

模型、用户命令、Agent 工具调用，都只是 Intent 的不同入口。

```text
用户 /it          → IntentInvocation
Agent tool call   → IntentCall
                         ↓
                   同一个 Intent 体系
```

---

## 2. 核心概念

| 概念 | 含义 |
|---|---|
| `Intent` | 一个可执行能力声明 |
| `IntentCall` | 一次对 Intent 的调用 |
| `IntentResult` | Intent 调用的结果 |
| `ExecuteResult` | 原生执行器的结果 |
| `IntentCaller` | 负责 `call Intent` |
| `NativeExecutor` | 负责 `execute` 原生功能 |
| `IntentRegistry` | Intent 定义集合 |
| `NativeRegistry` | 原生执行器集合 |

### call 与 execute

```text
call     = 调用 Intent
execute  = 执行原生功能
```

调用关系：

```text
call Intent
  ├─ Prompt Intent -> 交给模型
  ├─ Chain Intent  -> 递归 call
  └─ Native Intent -> execute 原生功能
```

外部 Agent 只能 `call Intent`，不能直接 `execute` 原生功能。

---

## 3. 数据模型

### 3.1 Intent

```cpp
struct Intent
{
    QString name;
    QString description;

    QString prompt;      // 用户意图（what）
    QString skill;       // 泛化方法（how）

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

### 3.2 IntentParameter

```cpp
struct IntentParameter
{
    QString name;
    QString type;              // string / number / boolean / object / array
    QJsonValue defaultValue;   // undefined = 必填
};
```

### 3.3 IntentStep

```cpp
struct IntentStep
{
    QString intent;        // 引用另一个 Intent
    QJsonObject arguments; // 参数，可包含 {"$param":"name"}
};
```

不再使用旧字段 `tool`。

### 3.4 IntentCall

```cpp
struct IntentCall
{
    QString id;
    QString name;
    QJsonObject arguments;
    QJsonObject metadata;   // tags / Intent
};
```

`IntentCall` 是核心调用记录。  
模型侧的 `tool_calls` 只是协议层另一种表示。

### 3.5 IntentResult

```cpp
struct IntentResult
{
    bool ok;
    QString content;
    QJsonObject data;
    QString error;
};
```

### 3.6 ExecuteResult

```cpp
struct ExecuteResult
{
    bool ok;
    QString content;
    QJsonObject data;
    QString error;
};
```

原生执行器返回 `ExecuteResult`，`IntentCaller` 再转换成 `IntentResult`。

---

## 4. 分层结构

```text
ulincore/
├── types/       基础类型
├── intent/      Intent 声明、解析、绑定、存储、call
├── executor/    原生 execute
├── model/       模型通信
│   └── protocol/ 模型 wire 协议适配
├── core/        状态机、会话、DSH 适配、Journal
└── settings/    配置
```

### 4.1 types

```text
IntentCall
IntentResult
ExecuteResult
Message
Usage
TurnResult
Role
```

### 4.2 intent

```text
UlinCoreIntent
UlinCoreInvocation
UlinCoreParser
UlinCoreBinding
UlinCoreIntentStore
UlinCoreIntentCaller
```

### 4.3 executor

```text
UlinCoreNativeExecutor
UlinCoreNativeRegistry
UlinCoreNativeImplementation
UlinCoreBuiltinNativeImplementations
UlinCoreBuiltinNativeExecutors
UlinCoreBuiltinIntents
```

### 4.4 model/protocol

```text
UlinCoreModelProtocol
```

只负责模型边界的 metadata 编解码，不参与核心 Intent 语义。

### 4.5 core

```text
UlinCoreKernel
UlinCoreMasterAgent
UlinCoreSession
UlinCoreJournal
```

---

## 5. /it 语法

```text
/it <name> {args} -> {metadata} <tail>
```

### 5.1 参数

```text
/it read_and_search {path:"/tmp/a", keyword:"QML"}
```

参数块：

```text
key:value
```

解析层只保留原文，绑定层再按 Intent schema 强转。

### 5.2 元数据

```json5
{
  prompt: "用 [style] 解释 [topic]",
  skill: "先给出定义，再给一个例子。",
  Intent: { /* 完整定义 */ },
  tags: ["asSkill", "save"]
}
```

非布尔值：

```text
prompt
skill
Intent
```

布尔状态：

```text
tags[]
```

### 5.3 Prompt / Skill

```text
prompt = 这次要做什么
skill  = 这类事情通常怎么做
```

`skill` 不能单独执行。  
`prompt + skill` 才能构成一个完整的 Skill Intent。

---

## 6. 标签系统

标签只写“偏离默认”的状态。

| 维度 | 默认 | 标签 |
|---|---|---|
| exec | auto | `asPrompt` / `asSkill` |
| save | false | `save` |
| author | disengaged | `agentEngaged` |
| skill | optional | `skillRequired` |
| define | 无 Intent 时执行已有；有 Intent 时 defineAndRun | `defineOnly` |

默认：

```text
exec   = auto
save   = false
author = disengaged
skill  = optional
define = 有 Intent -> defineAndRun
         无 Intent -> execute existing
```

### 6.1 执行模式

```text
无标签：
  executor 非空 -> Native
  steps 非空   -> Chain
  prompt 非空  -> Prompt

asPrompt：
  只使用 prompt

asSkill：
  使用 prompt + skill
  不执行 steps / executor
```

### 6.2 定义模式

只定义：

```text
tags:["defineOnly", "save"]
必须带 Intent
```

定义并执行：

```text
有 Intent，没有 defineOnly
默认 defineAndRun
```

Prompt-only 的 defineAndRun 拒绝：

```text
没有递归模型回合接收 prompt
```

### 6.3 冲突规则

```text
asPrompt + asSkill          拒绝
save + asPrompt/asSkill     拒绝
defineOnly + asPrompt/asSkill 拒绝
defineOnly + agentEngaged   拒绝
agentEngaged 没有 save      拒绝
skillRequired 没有 agentEngaged 拒绝
Intent + agentEngaged       拒绝
```

---

## 7. 用户 /it 调用链

```text
/it ...
  ↓
UlinCoreParser
  ↓
UlinCoreInvocation
  ↓
UlinCoreKernel::runParsed
  ↓
IntentStore
  ├─ Prompt  -> Model
  ├─ Native  -> IntentCaller -> NativeExecutor.execute
  ├─ Chain   -> IntentCaller -> 递归 call
  └─ Define  -> IntentStore
```

### 定义并执行

```text
/it name -> {Intent:{...}}
  ↓
addIntent()
  ↓
IntentCaller.call()
```

### 只定义

```text
/it name -> {Intent:{...}, tags:["defineOnly","save"]}
  ↓
addIntent()
  ↓
结束，不调用模型
```

---

## 8. Agent 调用链

模型协议中的 `tool_calls` 仍然是：

```json
{
  "name": "read_file",
  "arguments": { "path": "/tmp/a" }
}
```

协议层转换：

```text
tool_calls
  ↓
Protocol::decodeToolArguments
  ↓
IntentCall
  ↓
UlinCoreKernel::callIntent
  ↓
IntentCaller
  ├─ Native -> NativeExecutor.execute
  └─ Chain  -> 递归 call
```

核心层没有 `ToolCall`，只有 `IntentCall`。

### 8.1 metadata

工具参数中可带：

```json
{
  "path": "/tmp/a",
  "__ulincore_meta": {
    "tags": ["save"],
    "Intent": { ... }
  }
}
```

协议层：

```text
arguments.__ulincore_meta
        ↕
IntentCall.metadata
```

工具 schema 中也会注入这个可选参数。

---

## 9. 模型协议适配

### 9.1 模型实际收到什么

```text
system / user / assistant / tool 消息
+ tools 定义（name / description / JSON Schema）
```

模型输出：

```text
assistant 文本
+ tool_calls(name + arguments)
```

标准 OpenAI 风格没有顶层 metadata 字段。  
因此 ULin 使用保留参数：

```text
__ulincore_meta
```

### 9.2 编码

```text
IntentCall.metadata
  ↓ encodeToolArguments
arguments.__ulincore_meta
```

### 9.3 解码

```text
arguments.__ulincore_meta
  ↓ decodeToolArguments
IntentCall.metadata
```

### 9.4 限制

```text
这是 ULin 自有协议，不是 OpenAI 标准。
标准模型只有在 schema 提示下才会填这个参数。
不填时 Intent 仍然正常执行，只是 metadata 为空。
```

---

## 10. 内置 Intent

内置 Intent 在代码中声明：

```text
read_file
list_dir
write_file
search_text
```

每个 Intent 绑定：

```text
executor = builtin.read_file
executor = builtin.list_dir
executor = builtin.write_file
executor = builtin.search_text
```

执行路径：

```text
Intent
  ↓ executor ID
NativeRegistry
  ↓
NativeExecutor.execute
  ↓
ExecuteResult
  ↓
IntentResult
```

内置原生功能只暴露给内置 Intent。  
Agent 不直接调用 NativeExecutor。

---

## 11. 安全边界

```text
Agent 只能 call Intent
Agent 不能直接 execute Native
Agent 不能创建未知 executor
Agent 不能覆盖内置 Intent
Agent 不能绕过 allowWrites / allowShell
```

执行器负责：

```text
workspace 路径校验
allowWrites
allowShell
结果截断
超时/取消
```

Intent 负责：

```text
参数 schema
默认值
steps 引用
executor 绑定
```

Kernel 负责：

```text
调用顺序
递归深度
步数上限
Journal
```

---

## 12. 文件职责

### types

| 文件 | 职责 |
|---|---|
| `UlinCoreTypes.h/.cpp` | `IntentCall` / `IntentResult` / `ExecuteResult` / `Message` / `Usage` |

### intent

| 文件 | 职责 |
|---|---|
| `UlinCoreIntent.h/.cpp` | Intent 数据模型与 JSON |
| `UlinCoreInvocation.h` | `/it` 解析结果 |
| `UlinCoreParser.h/.cpp` | `/it` 语法、tags、Intent 块、冲突校验 |
| `UlinCoreBinding.h/.cpp` | 参数绑定、模板展开、类型强转 |
| `UlinCoreIntentStore.h/.cpp` | Intent 持久化 |
| `UlinCoreIntentCaller.h/.cpp` | `call Intent`，Chain 递归，Native 分派 |

### executor

| 文件 | 职责 |
|---|---|
| `UlinCoreNativeExecutor.h/.cpp` | 原生 execute 契约和注册表 |
| `UlinCoreNativeImplementation.h/.cpp` | 原生实现接口 |
| `UlinCoreBuiltinNativeImplementations.h/.cpp` | 四个内置原生功能 |
| `UlinCoreBuiltinNativeExecutors.h/.cpp` | 注册内置执行器 |
| `UlinCoreBuiltinIntents.h/.cpp` | 内置 Intent 声明 |

### model/protocol

| 文件 | 职责 |
|---|---|
| `UlinCoreModelProtocol.h/.cpp` | metadata ↔ arguments 编解码 |

### core

| 文件 | 职责 |
|---|---|
| `UlinCoreKernel.h/.cpp` | 状态机、Intent 分派、模型循环 |
| `UlinCoreMasterAgent.h/.cpp` | DSH 适配 |
| `UlinCoreSession.h/.cpp` | 会话消息 |
| `UlinCoreJournal.h/.cpp` | JSONL 事件账本 |

### model

| 文件 | 职责 |
|---|---|
| `UlinCoreModel.h` | 模型接口 |
| `UlinCoreEchoModel.h/.cpp` | 回环模型 |
| `UlinCoreOpenAiModel.h/.cpp` | OpenAI 兼容模型 |

### settings

| 文件 | 职责 |
|---|---|
| `UlinCoreSettings.h/.cpp` | provider/model/workspace/权限 |

---

## 13. 示例

### 13.1 创建 Prompt Intent

```text
/it demo.prompt {topic:"QML"} -> {
  Intent:{
    "description":"demo prompt",
    "prompt":"用一句话解释 [topic]",
    "skill":"",
    "parameters":[{"name":"topic","type":"string"}],
    "steps":[],
    "executor":""
  },
  tags:["defineOnly","save"]
}
```

调用：

```text
/it demo.prompt {topic:"QML"}
```

### 13.2 创建 Skill Intent

```text
/it demo.skill {topic:"QML"} -> {
  Intent:{
    "description":"demo skill",
    "prompt":"解释 [topic]",
    "skill":"先给出定义，再给一个例子。",
    "parameters":[{"name":"topic","type":"string"}],
    "steps":[],
    "executor":""
  },
  tags:["defineOnly","save"]
}
```

使用 Skill：

```text
/it demo.skill {topic:"QML"} -> {tags:["asSkill"]}
```

### 13.3 Chain Intent

```text
/it demo.list {path:"."} -> {
  Intent:{
    "description":"list demo",
    "prompt":"",
    "skill":"",
    "parameters":[{"name":"path","type":"string"}],
    "steps":[
      {"intent":"list_dir","arguments":{"path":{"$param":"path"}}}
    ],
    "executor":""
  }
}
```

### 13.4 原生 Intent

```text
/it list_dir {path:"."}
```

### 13.5 错误用例

```text
/it bad -> {tags:["defineOnly","save"]}
```

拒绝：缺少 `Intent` 定义。

```text
/it list_dir {path:"."} -> {tags:["asPrompt"]}
```

拒绝：`list_dir` 没有 prompt。

---

## 14. 当前状态

已完成：

```text
Intent 统一数据模型
IntentCall / IntentResult / ExecuteResult
IntentCaller.call 与 NativeExecutor.execute 分离
Parser tags / Intent / 冲突校验
Intent 持久化
Prompt / Skill / Chain / Native
模型 metadata 协议适配
tool/ 并入 executor/
```

过渡中：

```text
agentEngaged / skillRequired 仍未自动泛化
Intent 定义块目前按严格 JSON 解析，不是完整 JSON5
模型 metadata 依赖模型填写 __ulincore_meta
旧会话 JSON 字段做读取兼容
```

---

## 15. Intent 的动态创建

Intent 不是启动时写死的工具表。它可以在运行时被创建、保存、再次调用。

这是 ULinCore 扩展能力的核心：

```text
不需要重新编译
不需要重启
不需要改模型
Agent 可以在一次对话中定义新能力
```

### 15.1 动态创建路径

#### 用户定义

```text
/it demo.prompt {topic:"QML"} -> {
  Intent:{
    "description":"demo prompt",
    "prompt":"用一句话解释 [topic]",
    "parameters":[{"name":"topic","type":"string"}],
    "steps":[],
    "executor":""
  },
  tags:["defineOnly","save"]
}
```

行为：

```text
Parser 构造 Intent
→ Kernel 校验
→ addIntent()
→ IntentStore 内存更新
→ 写入 intents.json
→ 不执行
```

之后可以立即：

```text
/it demo.prompt {topic:"QML"}
```

#### 定义并执行

```text
/it demo.list {path:"."} -> {
  Intent:{
    "description":"list workspace",
    "prompt":"",
    "skill":"",
    "parameters":[{"name":"path","type":"string"}],
    "steps":[
      {"intent":"list_dir","arguments":{"path":{"$param":"path"}}}
    ],
    "executor":""
  }
}
```

没有 `defineOnly` 时：

```text
addIntent()
→ 立即 call 这个 Intent
```

#### Agent 定义

Agent 的工具调用可以携带 metadata：

```json
{
  "name": "demo.prompt",
  "arguments": {
    "__ulincore_meta": {
      "tags": ["defineOnly", "save"],
      "Intent": {
        "description": "demo",
        "prompt": "解释 [topic]",
        "parameters": [{"name": "topic", "type": "string"}],
        "steps": [],
        "executor": ""
      }
    }
  }
}
```

协议层解码成 `IntentCall.metadata`，Kernel 走同一条定义路径。

### 15.2 动态 Intent 的价值

```text
1. Agent 可以把一次成功操作固化成语义能力
2. 用户可以把自然语言流程保存成可复用 Prompt/Skill
3. Chain 可以引用刚创建的 Intent
4. Agent 可以组合已有原生能力，不需要写 C++
5. 能力目录在下一轮模型请求中自动更新
```

### 15.3 当前动态创建的边界

```text
Prompt-only Intent 会进入 IntentStore
但 executableToolSchemas() 默认只暴露 Native / Chain
所以 Prompt-only Intent 不一定会出现在模型工具表里
```

也就是说：

```text
用户 /it 能调用
Agent 直接 call 能调用
但模型不一定能自动发现 prompt-only 新 Intent
```

这是后续需要补的动态能力发现问题。

---

## 16. Intent 持久化

### 16.1 存储位置

```text
data/ulincore/intents.json
```

路径由 `UlinCoreMasterAgent` 设置：

```cpp
m_kernel->setIntentsPath(m_dataDir + "/intents.json");
```

如果没有设置路径：

```text
Intent 只存在内存
save() 直接返回成功
重启丢失
```

### 16.2 文件格式

严格 JSON 数组：

```json
[
  {
    "name": "demo.prompt",
    "description": "demo prompt",
    "prompt": "用一句话解释 [topic]",
    "skill": "",
    "executor": "",
    "parameters": [
      {"name": "topic", "type": "string"}
    ],
    "steps": []
  }
]
```

写入时按 `name` 排序。

### 16.3 加载流程

```text
setIntentsPath()
  ↓
IntentStore.load()
  ↓
清空内存
  ↓
读取 JSON 数组
  ↓
按 name 插入 QHash
  ↓
installBuiltinIntents()
```

关键行为：

- 文件不存在：允许，内存为空；
- JSON 不是数组：加载失败，记录 warning；
- Intent name 为空：跳过；
- 同名 Intent：后写入的覆盖先写入的；
- 内置 Intent 在用户 Intent 加载后安装，因此同名时内置优先。

### 16.4 保存流程

```text
addIntent()
  ↓
IntentStore.add()
  ↓
save()
  ↓
QSaveFile 原子写
  ↓
commit()
```

`QSaveFile` 保证：

```text
先写临时文件
→ 成功后才替换目标文件
→ 避免写一半导致 intents.json 损坏
```

保存前会创建目录。

### 16.5 addIntent 的语义

```cpp
void UlinCoreKernel::addIntent(const Intent& intent)
{
    m_intents.add(intent);
    if (!m_intentsPath.isEmpty())
        m_intents.save();
}
```

这意味着：

```text
内存先更新
磁盘后保存
```

如果保存失败：

```text
日志 warning
内存中的 Intent 仍然可用
重启后可能丢失
```

这是当前的一致性缺口。

### 16.6 内置 Intent 与用户 Intent

内置 Intent：

```text
read_file
list_dir
write_file
search_text
```

它们由代码生成，不写入 `intents.json`。  
每次启动重新安装。

用户 Intent：

```text
保存在 intents.json
启动时加载
运行时新增
```

当前同名策略：

```text
加载阶段：内置 Intent 覆盖用户同名 Intent
运行时：addIntent 可以写入同名，内存中会覆盖
重启后：又回到内置优先
```

所以“内置保护”还没有形成完整的运行时规则。

### 16.7 当前限制

```text
没有版本号
没有创建时间/来源
没有修改历史
没有回滚
没有删除 API
没有文件锁
没有 schema version
没有原子性的“先校验再落盘”
没有并发写保护
```

当前假设：

```text
单进程
单用户
单写者
```

### 16.8 建议的演进

```text
Intent version
createdBy / updatedBy
createdAt / updatedAt
source: user | agent | builtin
revision
删除/禁用
写入前完整校验
保存失败回滚内存
文件锁或单写者队列
```

这些不属于当前 MVP，但决定了动态 Intent 能否成为稳定的能力市场。

---

## 17. 一句话总结

```text
Intent 是唯一能力声明；
call 是调用 Intent；
execute 只属于原生执行器；
模型协议只负责把 IntentCall 映射成模型能理解的格式；
Intent 可以在运行时创建、保存，并在后续调用中成为新的能力。
```
