# ULinCore 架构（当前设计基线）

> 状态：当前设计基线，不是归档文档。  
> 适用范围：`source/src/shared/common/agent/ulincore/`、`source/include/shared/common/agent/ulincore/`。  
> 说明：文中 `/it`、`->{}` 等只是表达语义的示例语法，具体语法可以调整；架构约束不依赖这些符号。

## 1. 定位

ULinCore 不是单纯的模型循环，而是一个**可执行意图运行时**：

```text
用户 / Agent
    ↓
Invocation
    ↓
Resolver
    ↓
Intent
    ├─ Command 视图
    ├─ Tool 视图
    └─ Prompt 视图
    ↓
Execution
    ↓
Outcome
    ↓
可选：Reification（固化）
    ↓
Registry
```

核心判断：

- 工具、命令、提示词**不是同一种东西**。
- 统一的是它们背后的 **Intent**。
- 命令是人的入口，工具是 Agent 的入口，提示词是自然语言入口。
- 保存下来的行为是 **Recipe / Macro**，不是原始轨迹本身。
- 原始轨迹可以保存，但只能作为明确的字面量能力，不能冒充泛化能力。

一句话：

> ULinCore 统一的是“意图”，不是“输入形态”。

## 2. 目标与非目标

### 目标

1. 用户、Agent、系统共用一套能力注册表。
2. 用户可以用命令式文本直接调用能力。
3. Agent 可以用结构化 tool call 直接调用能力。
4. 提示词可以作为默认入口；命令识别失败时按明确规则回退。
5. 一次成功行为可以固化成以后可复用的命令/工具。
6. 原始输入保留，界面可以直接显示原始命令文本。
7. 解析结果可以供模型理解，但基础语法解析不依赖模型。
8. 所有实现可以收束在 `ulincore/` 内，不要求改外围通用解析器。

### 非目标

当前阶段不做：

- 任意应用生成器。
- 富内容块、结构化 UI、审批 UI。
- 跨设备同步、命令市场。
- 复杂表达式、变量计算、条件语言。
- 自动脱敏、权限审批、沙箱。
- Agent 在线修改解析语法。
- 把原始轨迹自动当作泛化 Recipe。

## 3. 分层

```text
┌─────────────────────────────────────┐
│ 1. 外围通用解析器                   │
│    wire 归一化，保留原始文本         │
└─────────────────────────────────────┘
                  ↓ RawInput
┌─────────────────────────────────────┐
│ 2. ULinCore 语义层                  │
│    二次解析、Intent、Recipe、        │
│    参数绑定、保存、系统提示词         │
└─────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────┐
│ 3. 执行层                           │
│    Tool / Prompt / Recipe / Model   │
└─────────────────────────────────────┘
                  ↓
┌─────────────────────────────────────┐
│ 4. 输出层                           │
│    当前保持纯文本事件与工具事件       │
└─────────────────────────────────────┘
```

### 3.1 外围通用解析器

只负责：

- DSH `content[]` → 原始文本
- 附件、来源等原始信息保留
- 不丢原始文本

不负责：

- 判断命令还是提示词
- 解析参数
- 决定工具
- 决定是否保存

### 3.2 ULinCore

负责：

- 二次语义解析
- Intent 解析与注册
- Recipe 存储
- 参数绑定
- 系统提示词组织
- 模型调用与工具执行编排
- 保存与注册
- 权限/副作用边界（后续增强）

### 3.3 模型

只负责：

- 理解 canonical intent
- 规划
- 生成 tool call
- 在需要时使用参数引用

不负责：

- 基础语法解析
- 决定是否保存
- 决定权限
- 修改解析规则
- 判断“这是不是命令”

## 4. 核心对象

### 4.1 Invocation

一次调用的原始事实：

```text
Invocation {
    rawText
    actor          user | agent | system
    intentName?
    args{}
    prompt?
    save?
    agentEngaged?
    source
}
```

约束：

- `rawText` 必须保留。
- `args` 是本次绑定值。
- `prompt` 是 canonical 描述或模板，不是第二套执行语义。
- `actor` 用于权限、保存策略和审计。

### 4.2 Intent

统一能力对象：

```text
Intent {
    id
    names[]
    description
    parametersSchema

    implementation:
        PromptTemplate?          // 仅提示词，没有调用链
        ToolCall / Recipe?       // 可执行调用链
        PromptTemplate + 调用链? // 两者都有，按模式选择

    defaultMode:
        prompt | chain

    exposure:
        command?
        tool?

    policy
    origin         builtin | user | agent
    version
}
```

原则：

- 一个 Intent 可以同时暴露成命令和工具。
- 命令、工具、提示词都只是视图，不是三份实现。
- 保存行为最终进入同一个 Intent Registry。
- **Prompt 本身也可以是一种 Intent 实现**，不是只能作为命令/工具的附属元数据。
- 只有 PromptTemplate、没有调用链的 Intent，是合法的 prompt-only Intent。
- 同时有 PromptTemplate 和调用链的 Intent，可以用 `AsPrompt` 选择走哪条路。

### 4.2.1 Prompt Intent

Prompt Intent 是“可复用的提示词能力”：

```text
Intent {
    name: "写作"
    implementation: PromptTemplate {
        text: "写个长篇小说"
    }
    defaultMode: prompt
    parametersSchema: {}
    exposure: { command: true, tool: false }
}
```

特点：

- 没有工具调用链。
- 执行方式是把模板展开后发给模型。
- 可以被保存、命名、复用。
- 可以带参数，例如 `"写一篇关于[主题]的小说"`。
- 默认 `AsPrompt=true`；显式 `AsPrompt=false` 且没有调用链时返回空。
- 作为 tool 暴露没有意义，除非它本身还带可执行链；默认只作为 command/prompt 入口。


### 4.3 Recipe / Macro

保存下来的行为：

```text
Recipe {
    name
    parameters[]
    constants{}
    steps[]
    bindings
    mode: literal | generalized
    policy
    origin
}
```

两种模式：

```text
literal       字面量宏，直接复用工具执行链
generalized   模型整理出的泛化能力
```

### 4.4 Binding

参数绑定：

```json
{
  "directory": { "$param": "directory" }
}
```

默认使用参数引用。

字面量必须显式：

```json
{
  "directory": "C:/Windows/Temp",
  "forceLiteral": ["directory"],
  "reason": "..."
}
```

## 5. 解析与执行流程

```text
RawInput
  │
  ├─ 显式 intent 前缀？
  │     ├─ 是 → 尝试解析 Intent
  │     │        ├─ 成功 → IntentInvocation
  │     │        └─ 无意义命令 → Prompt fallback
  │     └─ 否 → Prompt
  │
  ├─ 解析参数和元数据
  │
  ├─ 加载 Intent / 已保存命令
  │
  ├─ 构造 canonical prompt 和系统提示词
  │
  ├─ 选择执行模式
  │     ├─ AsPrompt=true      → 展开 PromptTemplate → 模型
  │     ├─ 有调用链且未要求 prompt → Tool / Recipe
  │     └─ 无调用链            → 空结果（可记录诊断日志）
  │
  └─ 成功且要求保存
        ├─ prompt-only      → PromptIntent
        ├─ agentEngaged=true  → 泛化 Recipe
        └─ agentEngaged=false → 字面量宏
              ↓
        注册为 Intent
              ↓
        command / tool 暴露
```

关键点：

- 基础解析必须确定性。
- 模型只处理 canonical intent，不处理“这是不是命令”的基础判断。
- 直接命令执行必须复用 Kernel 的工具执行链，不能绕过取消、日志和事件。
- Prompt-only Intent 是合法实现，不一定需要调用链。
- `AsPrompt` 只选择执行方式，不改变 Intent 的注册身份。

## 6. 入口语义

### 6.1 显式 Intent 入口

示例语法（可调整）：

```text
/it ...
```

语义：

```text
先尝试 Intent 解析
失败则退回 Prompt
```

它不是“所有文本先猜命令”，而是“显式尝试意图解析”。

### 6.2 元数据

示例：

```text
/it cleanmypc{directory:"c:/"}
  -> {
       "prompt": "帮我清理[directory]",
       "isSave": true,
       "isAgentEngaged": true
     }
```

字段语义：

| 字段 | 语义 |
|---|---|
| `prompt` | 本次意图的 canonical 描述或模板（what） |
| `isSave` | 成功后是否固化成可复用能力 |
| `isAgentEngaged` | 固化时是否让 Agent 设计处理方案（prompt / skill / steps / executor 可组合） |
| `hasSkill` | 强制 Agent 方案必须包含 skill 字段；仅在 isAgentEngaged=true 时有意义 |
| `AsPrompt` | 本次是否只按 PromptTemplate 展开 |
| `AsSkill` | 本次是否使用 prompt + 已保存的 skill（规划中） |

约束：

- `prompt` 不能覆盖命令参数的执行语义；如果它改变执行行为，应产生新的 Intent。
- `isSave` 不是模型一句话就能触发；由解析层/Kernel 处理。
- `isAgentEngaged=true` 时，Agent 可以决定优化 prompt、补充 skill、构造 steps、绑定已有 executor，或多个组合。
- `hasSkill=true` 表示 Agent 方案必须包含非空 `skill`；没有生成出 skill 则保存失败/回退。
- `hasSkill=true` 但 `isAgentEngaged=false` 时忽略（或按宿主策略报错），不产生 skill。
- `isAgentEngaged` 控制固化方式，不控制基础解析是否经过模型。
- `AsPrompt=true` 要求 Intent 有 PromptTemplate；没有则报错。
- `AsPrompt` 未指定时：prompt-only Intent 默认走 prompt；带调用链的 Intent 默认走 chain。
- `AsSkill=true` 要求 prompt 和 skill 都非空；skill 不能单独执行。

### 6.3 Prompt fallback

- `/it` 后没有有意义的命令：按 Prompt 处理。
- 普通 `/` 命令如果失败：应报错，不应静默变成聊天。
- 裸文本：默认按 Prompt 处理。
- Prompt-only Intent 复用：`AsPrompt=true` 时展开 PromptTemplate 发给模型。
- Prompt-only Intent 且 `AsPrompt=false`：没有调用链，返回空。

## 7. 参数绑定

### 7.1 参数名是符号，值是绑定

```text
copy{from="/tmp/a", to="/tmp/b"}
```

解析结果：

```text
args: {
    from: "/tmp/a"
    to: "/tmp/b"
}
```

`from` 是参数名，`/tmp/a` 是本次绑定值。

不需要写成：

```text
from=$from
```

`$param` 是引用表达式，只有需要派生或跨步骤引用时才需要。

### 7.2 模型与工具调用的约定

模型可以看到：

- 参数名
- 参数类型
- 参数描述
- 必填/默认值

模型生成 tool call 时默认使用引用：

```json
{
  "from": { "$param": "from" },
  "to": { "$param": "to" }
}
```

工具调用器在执行前解析：

```text
resolveParams(args, parameters)
```

工具本身只读取最终值。

### 7.3 字面量升级

默认行为：

```text
参数化字段收到字面量 → 拒绝
返回 PARAM_REF_REQUIRED
```

Agent 思考后可以显式强制：

```json
{
  "args": { "directory": "C:/Windows/Temp" },
  "forceLiteral": ["directory"],
  "reason": "..."
}
```

约束：

- 必须有显式 `forceLiteral`。
- 必须有原因。
- 最多重试一次，避免循环。
- 工具调用器仍需做权限和副作用检查。
- 强制字面量不等于允许执行危险操作。

### 7.4 保存时的区分

保存时必须区分：

```text
parameters   可覆盖的参数
constants    固定值
```

如果 Agent 强制了字面量，保存时要明确它是本次 override 还是永久 constant，不能默认写成参数默认值。

### 7.5 非 Agent 模式

`isAgentEngaged=false` 时：

- 不要求模型制定行为。
- 直接复用工具执行链。
- 保存为字面量宏。
- 参数使用显式槽位和默认值。
- 原始轨迹可以保存，但只能标记为 `literal`。

## 8. 保存模型

保存只发生在成功执行之后：

```text
成功 Invocation
  │
  ├─ isSave=false
  │     └─ 不进入 Registry
  │
  └─ isSave=true
        │
        ├─ 无调用链，保存为 PromptIntent
        │     → 可复用 PromptTemplate
        │     → 默认 AsPrompt=true
        │
        ├─ 有调用链且 agentEngaged=true
        │     → 模型整理成 generalized Recipe
        │     → 参数化、步骤化、可回放
        │
        └─ 有调用链且 agentEngaged=false
              → 保存 literal macro
              → 参数显式、常量显式
              → 直接复用工具链
```

保存规则：

1. 只保存成功执行的行为。
2. 原始轨迹可以存，但必须标记为 literal。
3. 泛化 Recipe 必须参数化、可验证、可回放。
4. 固定值和参数必须分开。
5. Prompt-only 行为保存为 PromptIntent，不冒充工具链。
6. 保存后注册为 Intent，可暴露为 command 和/或 tool；PromptIntent 默认只暴露 command。
7. 同名冲突必须有命名空间。
8. 保存的命令作为工具注册时，`parametersSchema()` 暴露形式参数，不暴露具体值。

## 9. Registry 与暴露

全系统只有一个 Intent Registry：

```text
IntentRegistry
  ├─ builtin tools
  ├─ user commands
  ├─ prompt intents
  ├─ agent-created recipes
  └─ saved macros
```

规则：

- 命令和工具不能各有一套注册表。
- 已保存命令既可以是 command，也可以是 tool。
- 同名冲突必须有命名空间。
- 来源、版本、权限必须记录。

暴露模型：

```text
Intent
  ├─ expose.command = true
  └─ expose.tool = true
```

同一个 Intent 的两种入口共享实现和权限，不重复存储。

## 10. 系统提示词与模型职责

系统提示词由 ULinCore 动态组织：

```text
基础系统提示词
  + 当前 Intent/Command 目录
  + 本次解析结果说明
  + 参数绑定规则
```

要求：

- 不把所有历史命令无限塞进上下文。
- 只暴露与当前任务相关的命令/工具。
- 明确要求模型对参数化字段使用 `$param` 引用，而不是硬编码。
- 模型输出仍需工具调用器校验，不能只靠提示词约束。

## 11. 与现有代码的边界

当前 ULinCore 已具备：

- `UlinCoreKernel` 回合状态机
- `UlinCoreToolRegistry` 工具注册
- `UlinCoreSession` 消息账本
- `UlinCoreMasterAgent` DSH 适配
- OpenAI 兼容模型适配
- 会话持久化

目标边界：

```text
MasterAgent：wire 解析，把 DSH 输入变成原始文本
ULinCore：二次语义解析、Intent、Recipe、参数绑定
Model：理解 canonical intent，生成规范化工具调用
Tool caller：解析参数引用，执行工具
```

最小实现原则：

- 外围通用解析器不改。
- `MasterAgent` 协议不改，只把原始文本交给 ULinCore。
- `Session` 保持原始文本/现有 Message，不要求新 schema。
- `Model` 接口不改。
- 输出仍保持当前纯文本事件和工具事件。
- QML 不需要为新范式改造。
- 所有新增代码收在 `ulincore/` 目录内。

建议新增的内部组件：

```text
UlinCoreParser
UlinCoreInvocation
UlinCoreIntentRegistry
UlinCoreRecipeStore
UlinCoreCommandTool
UlinCoreBinding
```

## 12. 最小实现顺序

1. 原始文本入口与二次解析骨架。
2. 参数解析：`name=value` 形式，参数名 + 绑定值。
3. Intent 注册与命令执行。
4. Prompt Intent：PromptTemplate、`AsPrompt` 和执行模式选择。
5. 工具调用参数引用 `$param` 与工具调用器解析。
6. `forceLiteral` 显式升级。
7. Prompt Intent 保存与注册。
8. 字面量宏保存与注册。
9. 泛化 Recipe 保存（`isAgentEngaged=true`）。
10. 系统提示词中注入命令目录与参数规则。
11. 保存命令注册为 Tool。
12. 后续再加权限、版本、脱敏、富内容。

## 13. 架构不变量

1. 一个 Intent Registry。
2. 原始文本必须保留。
3. 基础解析必须确定性，不交给模型。
4. Prompt 不能成为第二执行语义。
5. Prompt Intent 是合法实现，不一定需要调用链。
6. `AsPrompt=true` 要求存在 PromptTemplate；`AsPrompt=false` 且无调用链时返回空。
7. 参数默认使用引用，字面量必须显式。
8. 原始轨迹只能作为 literal macro，不能冒充 generalized recipe。
9. 模型不负责保存、权限、语法规则。
10. 工具调用器负责参数绑定，工具本身只读最终值。
11. 保存成功后才能进入 Registry。
12. 命令/工具/Recipe 的副作用必须有边界。
13. 直接命令执行必须复用 Kernel 的工具执行链。
14. 所有新增实现收在 `ulincore/` 内。

## 14. 示例

### 14.1 泛化保存

```text
/it cleanmypc{directory:"c:/"}
  -> {
       "prompt": "帮我清理[directory]",
       "isSave": true,
       "isAgentEngaged": true
     }
```

执行：

```text
解析 intent cleanmypc
绑定 directory = "c:/"
模型根据 canonical prompt 制定清理行为
成功后保存为 generalized Recipe
注册成 command/tool
```

### 14.2 字面量保存

```text
/it copy{from:"/tmp/a", to:"/tmp/b"}
  -> {
       "prompt": "复制 [from] 到 [to]",
       "isSave": true,
       "isAgentEngaged": false
     }
```

执行：

```text
解析 intent copy
绑定 from/to
直接调用现有 copy 工具链
保存为 literal macro
以后可用新参数覆盖
```

### 14.3 参数引用

模型生成：

```json
{
  "from": { "$param": "from" },
  "to": { "$param": "to" }
}
```

工具调用器解析后：

```json
{
  "from": "/tmp/a",
  "to": "/tmp/b"
}
```

### 14.4 首次创建 Prompt Intent

```text
/it 写作
  -> {
       "prompt": "写个长篇小说",
       "isSave": true,
       "isAgentEngaged": false
     }
```

执行：

```text
解析 intent 写作
没有现成调用链
isAgentEngaged=false：不要求 Agent 制定规则
保存为 PromptIntent
PromptTemplate = "写个长篇小说"
本次按 Prompt 执行，发给模型
```

### 14.5 复用 Prompt Intent

```text
/it 写作
  -> {
       "AsPrompt": true
     }
```

执行：

```text
查找 Intent: 写作
AsPrompt=true
展开 PromptTemplate
把 "写个长篇小说" 发给模型
```

如果省略 `AsPrompt`：

```text
prompt-only Intent 默认走 prompt
```

### 14.6 Prompt Intent 没有调用链

```text
/it 写作
  -> {
       "AsPrompt": false
     }
```

执行：

```text
查找 Intent: 写作
AsPrompt=false
没有调用链
返回空结果
```

这里返回空不是错误，而是这个 Intent 本来就没有可执行链。实现上可以记录一条诊断日志，但不产生模型输出。

## 15. 当前不做、以后再加

- 脱敏
- 权限审批 UI
- 沙箱
- 富内容块
- 结构化 UI 指令
- 复杂表达式
- 版本迁移
- 命令市场/共享
- Agent 在线修改解析语法
- 多模态输入

这些都不影响当前架构成立；后续可以在不改变核心对象的前提下增量加入。

## 16. 设计决策摘要

| 决策 | 结论 |
|---|---|
| 统一对象 | 统一 Intent，不统一 command/tool/prompt 类型 |
| Intent 实现 | PromptTemplate、ToolCall/Recipe，或两者兼有 |
| Prompt Intent | 合法的一等 Intent，可保存、可复用、可带参数 |
| 执行选择 | `AsPrompt` 选择 PromptTemplate 或调用链 |
| 无调用链 | `AsPrompt=false` 时返回空，不报错 |
| 原始文本 | 保留，作为显示和审计根 |
| 基础解析 | 确定性代码，不经过模型 |
| Prompt | canonical 描述/模板，不是第二执行语义 |
| 参数 | 名字是符号，值是绑定 |
| 模型传参 | 默认 `$param` 引用，不硬编码 |
| 字面量 | 显式 `forceLiteral`，有原因、有上限 |
| 保存 | 成功后才保存；prompt / literal / generalized 三种结果 |
| 原始轨迹 | 可保存，但只能是 literal macro |
| 命令注册 | 保存后作为 Intent，可暴露为 command/tool |
| 代码边界 | 新增实现收在 `ulincore/`，外围不改 |
| 当前输出 | 保持纯文本事件，不做富内容/审批 UI |

## 17. 当前实现状态（MVP）

已落地在 `ulincore/`：

- `/it` 二次解析，普通文本保持 Prompt fallback。
- Prompt-only Intent：保存、复用、`AsPrompt` 展开、无调用链时返回空。
- 参数解析、Prompt 模板展开 `[name]`。
- 直接工具命令：复用现有 `UlinCoreTool` 执行链。
- 字面量 Intent 保存：命令名命中已保存 Intent；参数引用 `$param` 绑定。
- 已保存的有链 Intent 注册为 `UlinCoreTool`，Agent 可通过 tool call 调用。
- `intents.json` 持久化，Kernel 启动时加载并注册工具。
- Recipe 工具参数解析和递归深度保护。

当前仍是 MVP 的部分：

- `isAgentEngaged=true` 目前保存为 PromptIntent，由模型在后续执行；尚未在保存时额外生成一份结构化的 generalized Recipe。
- `forceLiteral` 的拒绝/升级流程尚未接入模型 tool call；当前参数引用主要用于保存 Recipe 的步骤绑定。
- 保存的 Session 消息使用 canonical Prompt；原始命令文本由外围事件保留，尚未在 Session 中单独保存 raw display 字段。
- 权限、脱敏、版本迁移、富内容/审批 UI 仍按文档中的“以后再加”处理。

