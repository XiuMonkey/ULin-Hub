# ULinCore Intent / `/it` 规格

> 状态：与当前实现同步。
> 本文只描述已实现行为；未实现能力单独列在“当前限制”。

## 1. 核心模型

```text
Intent = 一个可执行能力声明
```

```cpp
Intent
  name
  description
  parameters[]
  prompt      // 用户意图（what）
  skill       // 泛化方法（how）
  steps[]     // 确定性调用链：{ intent, arguments }
  executor    // 客户端原生执行器 ID
```

- `prompt` 表达要做什么，`skill` 表达通常怎么做。
- `skill` 不能单独执行。
- `steps` 引用其他 Intent，不保存调用 id/result。
- `executor` 是叶子实现，不声明 schema。

## 2. `/it` 语法

```text
/it <name> {key:value, ...} -> {prompt:..., skill:..., Intent:{...}, tags:[...]} <tail>
```

- 参数块和元数据块统一用 `:`。
- `/it` 必须在开头；`/itfoo` 是普通 Prompt。
- 参数值先保存原文，绑定层再按 `parameters` schema 强转。
- 参数和元数据块都是**严格 JSON**；不支持注释、尾逗号等 JSON5 语法。

```text
/it summarize {topic:"QML", max_items:"5", strict:"false"}
```

原文：

```json
{ "topic": "QML", "max_items": "5", "strict": "false" }
```

绑定后：

```json
{ "topic": "QML", "max_items": 5, "strict": false }
```

执行器只接收绑定后的 typed arguments。

## 3. 元数据

只有两类：

```text
非布尔值：prompt / skill / Intent
布尔状态：tags[]
```

### 3.1 非布尔键值对

```json
{
  "prompt": "用 [style] 解释 [topic]",
  "skill": "先确认读者，再按定义、机制、常见误区组织。",
  "Intent": { "description": "解释主题" }
}
```

| 字段 | 作用 |
|---|---|
| `prompt` | 创建 PromptIntent，或作为旧数据的兼容入口 |
| `skill` | 创建 PromptIntent 时一并保存方法 |
| `Intent` | 完整 Intent 定义块 |

当前执行模式下：

```text
asPrompt    拒绝外部 prompt / skill
asSkill     必须提供外部 prompt；skill 可选
withSkill   拒绝外部 prompt / skill
```

### 3.2 单数组标签

```json
{
  "tags": ["asPrompt", "save", "agentEngaged", "skillRequired"]
}
```

标签只写“偏离默认”的状态；默认值不写标签。

| 维度 | 默认 | 非默认标签 |
|---|---|---|
| exec | `auto`：Native / Chain / Prompt | `asPrompt` / `asSkill` / `withSkill` |
| save | 不保存 | `save` |
| author | 不让 Agent 参与泛化 | `agentEngaged` |
| skill | skill 可选 | `skillRequired` |
| define | 有 `Intent` 时定义并执行；无 `Intent` 时执行已有 | `defineOnly` |

默认值：

```text
exec   = auto
save   = false
author = disengaged
skill  = optional
define = 有 Intent -> defineAndRun
         无 Intent -> 执行已有
```

### 3.4 三类 `/it` 语义

这三类不是代码里的 `enum`，而是由元数据组合自然形成的语义类型。

#### 定义型

```text
有完整 Intent 定义
定义是硬写的
默认：定义 + 执行
defineOnly：只定义，不执行
```

```text
/it demo {args} -> { Intent:{...} }
/it demo {args} -> { Intent:{...}, tags:["defineOnly","save"] }
```

特点：

```text
定义完整
定义后即可作为执行型使用
只能执行 Native / Chain
Prompt-only 的 defineAndRun 拒绝
```

#### 生成型

```text
没有完整 Intent
先有 seed prompt / skill
由 Agent 的操作链或 Agent 参与生成 Intent
生成前不能当作执行型
不能使用执行型字段/标签
```

两种模式：

```text
agentEngaged   先执行 seed，再根据执行结果生成 Intent
generateOnly    不执行 seed，只生成 Intent
```

`generateOnly` 是规划中的标签，当前未实现。

特点：

```text
prompt 是生成种子
skill 可选，作为生成约束
生成结果需要 save 落盘
生成完成前不能直接执行
```

#### 执行型

```text
Intent 已存在
只提供参数
可选 asPrompt / asSkill / withSkill
```

```text
/it demo {args}
/it demo {args} -> {tags:["asPrompt"]}
/it demo {args} -> {prompt:"...", tags:["asSkill"]}
/it demo {args} -> {tags:["withSkill"]}
```

#### 三类对照

| 字段 / 标签 | 定义型 | 生成型 | 执行型 |
|---|---|---|---|
| `Intent` | 必需 | 禁止 | 禁止 |
| `prompt` | 定义内容 | 生成 seed，必需 | 仅 `asSkill` 外部 prompt |
| `skill` | 定义内容 | 生成约束，可选 | 仅 `asSkill` 外部 skill |
| `steps` / `executor` | 在 `Intent` 内 | 禁止 | 禁止（来自已存 Intent） |
| `save` | 可选；当前定义块总是保存 | 必需，保存生成结果 | 禁止 |
| `defineOnly` | 可选：只定义 | 禁止 | 禁止 |
| `generateOnly` | 禁止 | 可选：只生成不执行 | 禁止 |
| `agentEngaged` | 禁止 | 默认模式：先执行再生成 | 禁止 |
| `skillRequired` | 禁止 | 可选：生成结果必须含 skill | 禁止 |
| `asPrompt` | 禁止 | 禁止 | 可选 |
| `asSkill` | 禁止 | 禁止 | 可选 |
| `withSkill` | 禁止 | 禁止 | 可选 |
| 参数 | 定义参数 / 执行参数 | 生成 seed 参数 | 调用参数 |
| 调用方 | 用户 / Agent | 用户 / 宿主；Agent 不能调用 | 用户 / Agent |

#### 调用方边界

```text
用户 / 宿主：
  定义型、生成型、执行型

Agent：
  定义型、执行型
  不能调用生成型
```

原因：

```text
生成型本身就是 Agent 参与生成的行为
Agent 不能再把“生成”委托给另一个 Agent
以后有子代理时再单独定义委派规则
```

#### 关键边界

```text
定义型完整 -> 可以立即执行
生成型不完整 -> 生成前不能执行
执行型只消费已存在的 Intent
```

生成型不能使用：

```text
Intent
asPrompt
asSkill
withSkill
steps
executor
```

这些都属于“已经定义完成”的语义。

## 4. 执行模式

### 4.1 默认 `auto`

已有 Intent，裸执行：

```text
executor 非空 -> Native
steps 非空    -> Chain
prompt 非空   -> Prompt
```

### 4.2 `asPrompt`

```text
只发 Intent 内置 prompt
忽略 skill / steps / executor
不接受外部 prompt / skill
```

要求：

```text
Intent.prompt 非空
```

### 4.3 `asSkill`

```text
使用外部 prompt
skill 优先使用外部 skill，否则使用 Intent.skill
不执行 steps / executor
```

规则：

```text
必须提供外部 prompt
不能回退到 Intent.prompt
skill 可以由 Intent 内置，也可以外部提供
prompt 或 skill 为空则失败
```

示例：

```text
/it demo.explain2 {topic:"deepseek"} -> {
  prompt:"用中文解释 [topic]。",
  tags:["asSkill"]
}
```

实际发送：

```text
用中文解释 deepseek。

[Skill]
<Intent.skill 或外部 skill>
```

### 4.4 `withSkill`

```text
只使用 Intent 内置 prompt + 内置 skill
不接受外部 prompt / skill
不执行 steps / executor
```

要求：

```text
Intent.prompt 非空
Intent.skill  非空
```

示例：

```text
/it demo.explain2 {topic:"deepseek"} -> {tags:["withSkill"]}
```

### 4.5 三者的核心区别

| 标签 | prompt | skill |
|---|---|---|
| `asPrompt` | 内置 | 忽略 |
| `asSkill` | **必须外部** | 外部优先，否则内置 |
| `withSkill` | 内置 | 内置 |

## 5. 标签冲突

### 5.0 完整冲突矩阵

图例：

```text
✓  可同时出现，组合有意义
△  可解析，但当前被忽略/无实际效果，或需要额外条件
✗  拒绝
—  不适用；同一行/列是自身，重复标签本身也会被拒绝
↔  与上三角对称，见对应格子
```

| 行 \ 列 | prompt | skill | Intent | asPrompt | asSkill | withSkill | save | defineOnly | agentEngaged | skillRequired |
|---|---|---|---|---|---|---|---|---|---|---|
| **prompt** | — | ✓ 可共存 | △ 定义时忽略 prompt | ✗ 禁止外置 | ✓ asSkill 必需 | ✗ 禁止外置 | ✓ 创建并保存 | ✗ 需要 Intent | △ 需 save，暂无泛化 | ✗ 需要 agentEngaged |
| **skill** | ↔ | — | △ 定义时忽略 skill | ✗ 禁止外置 | ✓ 外部 skill 覆盖 | ✗ 禁止外置 | △ 单独 skill 无创建路径 | ✗ 需要 Intent | △ 需 prompt/save | ✗ 需要 agentEngaged |
| **Intent** | ↔ | ↔ | — | ✗ 定义 + 执行冲突 | ✗ 定义 + 执行冲突 | ✗ 定义 + 执行冲突 | ✓ 可保存；当前定义总是保存 | ✓ 只定义并保存 | ✗ 定义块禁止泛化 | ✗ 定义块禁止泛化 |
| **asPrompt** | ↔ | ↔ | ↔ | — | ✗ 与 asSkill 冲突 | ✗ 与 withSkill 冲突 | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ agent 需要 save，冲突 | ✗ 需要 agent + save |
| **asSkill** | ↔ | ↔ | ↔ | ↔ | — | ✗ 与 withSkill 冲突 | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ save 冲突 | ✗ save 冲突 |
| **withSkill** | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ save 冲突 | ✗ save 冲突 |
| **save** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✓ defineOnly 必需 | ✓ agentEngaged 必需 | ✓ 经 agent 要求；需 agentEngaged |
| **defineOnly** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✗ 定义块禁止泛化 | ✗ 定义块禁止泛化 |
| **agentEngaged** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✓ skillRequired 依赖 agentEngaged；需 save |
| **skillRequired** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — |

### 5.1 矩阵说明

```text
prompt + Intent
  当前定义路径忽略 prompt override，实际使用 Intent.prompt；
  如果要覆盖 prompt，应使用 asSkill 的外部 prompt。

skill + Intent
  当前定义路径忽略外部 skill，实际使用 Intent.skill；
  如果要覆盖 skill，应使用 asSkill 的外部 skill。

skill + save
  单独 skill 不能创建 PromptIntent；没有 prompt 时不会保存。
  当前会退化为原始 Prompt fallback，skill 被忽略。

prompt + agentEngaged / skillRequired
  解析可通过，但当前没有自动泛化实现；
  实际效果仍需后续补全。
```

### 执行标签互斥

```text
asPrompt + asSkill     拒绝
asPrompt + withSkill   拒绝
asSkill + withSkill    拒绝
```

### 与 save 冲突

```text
save + asPrompt        拒绝
save + asSkill         拒绝
save + withSkill       拒绝
```

执行标签用于执行，`save` 用于保存，不能同一次调用同时做。

### 与 Intent 定义块冲突

```text
Intent + asPrompt      拒绝
Intent + asSkill       拒绝
Intent + withSkill     拒绝
```

### defineOnly

```text
defineOnly 必须有 Intent:{...}
defineOnly 必须带 save
defineOnly + asPrompt      拒绝
defineOnly + asSkill       拒绝
defineOnly + withSkill     拒绝
defineOnly + agentEngaged  拒绝
defineOnly + skillRequired 拒绝
```

### agentEngaged / skillRequired

```text
agentEngaged 必须带 save
skillRequired 必须带 agentEngaged
skillRequired 必须带 save
二者不能和 Intent 定义块同用
```

## 6. 只定义

```text
/it summarize -> {
  Intent:{
    "description":"解释主题",
    "prompt":"用 [style] 解释 [topic]",
    "skill":"",
    "parameters":[
      { "name":"topic", "type":"string" },
      { "name":"style", "type":"string", "default":"简洁" }
    ],
    "steps":[],
    "executor":""
  },
  tags:["defineOnly", "save"]
}
```

语义：

```text
校验 Intent
→ addIntent()
→ 写 intents.json
→ 不执行
→ 不发给 Agent
```

`name` 由 `/it name` 提供，不在 `Intent` 块里重复。

## 7. 定义并执行

```text
/it summarize -> {
  Intent:{...}
}
```

有 `Intent` 且没有 `defineOnly` 时，默认 `defineAndRun`。

语义：

```text
校验 Intent
→ addIntent()
→ 执行
```

限制：

```text
只允许 Native / Chain
Prompt-only 拒绝
不能叠加 as* / withSkill / agentEngaged / skillRequired
```

Prompt-only 拒绝的原因：

```text
定义并执行 prompt-only Intent
→ 需要再起一个模型回合
→ 当前回合没有明确接收者
→ 递归 / 重复上下文
```

Prompt-only 应保存，之后由用户执行；Agent 调用同一 Intent 时只拿渲染结果，不递归起模型回合。

## 8. 通过 prompt 新建 Intent

这是最常用的动态创建形式：

```text
/it demo.explain2 {topic:"deepseek"} -> {
  prompt:"解释 [topic]。",
  skill:"先给出定义，再给一个例子。",
  tags:["save"]
}
```

行为：

```text
名称 demo.explain2 不存在
→ 有 prompt
→ 创建 PromptIntent
→ 保存 prompt + skill
→ 立即执行一次
```

注意：

```text
PromptIntent 创建路径只受 save 控制
asPrompt / asSkill / withSkill 不能用于这次创建
```

创建后调用：

```text
/it demo.explain2 {topic:"deepseek"} -> {tags:["asPrompt"]}
/it demo.explain2 {topic:"deepseek"} -> {
  prompt:"用中文解释 [topic]。",
  tags:["asSkill"]
}
/it demo.explain2 {topic:"deepseek"} -> {tags:["withSkill"]}
```

## 9. 用户与 Agent

只有一个 IntentStore、一个 `IntentInvocation`、一个 dispatch。

```text
用户：/it name {args}
Agent：tool call { name, arguments, metadata }
        ↓
   IntentInvocation
        ↓
resolveIntent → 参数绑定 → 执行/渲染
```

### 用户

```text
/it name {args}                          执行已有 Intent
/it name {args} -> {tags:["asPrompt"]}   内置 prompt only
/it name {args} -> {prompt:"...", tags:["asSkill"]}
                                          外部 prompt + skill
/it name {args} -> {tags:["withSkill"]}   内置 prompt + skill
/it name -> {Intent:{...}, tags:["defineOnly","save"]}
                                          只定义
/it name -> {Intent:{...}}               定义并执行
```

### Agent

Agent 直接调用 Intent，不使用包装 API。

普通执行：

```json
{ "name": "read_and_search", "arguments": { "path": "/tmp/a", "keyword": "QML" } }
```

`asSkill`：

```json
{
  "name": "summarize",
  "arguments": { "topic": "QML" },
  "metadata": {
    "prompt": "用中文解释 [topic]。",
    "tags": ["asSkill"]
  }
}
```

`withSkill`：

```json
{
  "name": "summarize",
  "arguments": { "topic": "QML" },
  "metadata": { "tags": ["withSkill"] }
}
```

`asPrompt`：

```json
{
  "name": "summarize",
  "arguments": { "topic": "QML" },
  "metadata": { "tags": ["asPrompt"] }
}
```

定义：

```json
{
  "name": "summarize",
  "metadata": {
    "tags": ["defineOnly", "save"],
    "Intent": { "...": "完整 Intent 定义" }
  }
}
```

定义并执行：

```json
{
  "name": "summarize",
  "metadata": {
    "Intent": { "...": "完整 Intent 定义" }
  }
}
```

`metadata` 和 `/it` 的元数据是同一套语义，只是载体不同。

### Agent 约束

```text
不能请求 agentEngaged / skillRequired
不能和 save 同用执行标签
定义并执行只能 Native / Chain
不能创建未知 executor
不能覆盖内置 Intent
不能绕过 allowWrites / allowShell
```

## 10. agentEngaged / skillRequired

```text
agentEngaged
  执行成功后允许 Agent 设计处理方案

skillRequired
  是 agentEngaged 的约束
  要求方案必须包含非空 skill
```

当前只有解析和校验，没有自动泛化。

## 11. 持久化

位置：

```text
data/ulincore/intents.json
```

格式：

```json
[
  {
    "name": "demo.explain",
    "description": "Saved prompt intent",
    "prompt": "解释 [topic]。",
    "skill": "先给出定义，再给一个例子。",
    "parameters": [{ "name": "topic", "type": "string" }],
    "steps": [],
    "executor": ""
  }
]
```

加载：

```text
IntentStore.load()
→ 安装内置 Intent
→ 同名用户 Intent 不覆盖内置
```

保存：

```text
IntentStore.save()
→ QSaveFile 原子写
→ 按 name 排序
```

## 12. 当前限制

```text
Intent 定义块当前总是 addIntent() 保存；save 标签主要影响 PromptIntent 创建
Prompt-only Intent 不在 executableToolSchemas() 中，模型不能自动发现
agentEngaged / skillRequired 没有自动泛化
Intent 定义块是严格 JSON，不支持 JSON5
模型 metadata 依赖模型填写 __ulincore_meta
没有 Intent 版本 / 来源 / 时间 / 修改历史
没有删除 / 禁用 API
没有文件锁 / 并发写保护
保存失败只 warning，不回滚内存
```

## 13. 最小记忆

```text
/it name {参数} -> {prompt/skill/Intent + tags} tail
```

```text
asPrompt   内置 prompt，忽略 skill
asSkill    外部 prompt 必须，skill 可选
withSkill  内置 prompt + 内置 skill
save       保存，不能和执行标签同用
defineOnly 只定义，必须 Intent + save
```

```text
用户 /it 和 Agent tool call 都生成 IntentInvocation
Agent 直接调用 Intent；定义只是同一个 IntentInvocation 携带 Intent
```
