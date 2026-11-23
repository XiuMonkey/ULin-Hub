# ULinCore Intent / `/it` 规格

> 状态：本文描述目标规范；当前实现差异见“当前限制”。

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
withSkill   拒绝外部 prompt；外部 skill 可覆盖内置
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

这三类不是代码里的 `enum`，而是由 IntentStore 状态直接决定：

```text
有 Intent 元数据              -> 定义型
无 Intent + 无同名 Intent     -> 生成型
无 Intent + 有同名 Intent     -> 执行型
```

普通文本不参与这三类，仍是 Prompt。

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
由 Agent 的行为链，或 Agent 参与设计，生成 Intent
生成前不能当作执行型
不能使用执行型字段/标签
```

生成来源：

```text
1. 元数据：prompt / skill
2. Agent 行为链：实际 tool calls / steps
3. 可选 Agent 参与：agentEngaged
```

`agentEngaged` 不是生成型的必要条件。  
没有 `agentEngaged` 时，也可以直接把“元数据 + Agent 行为链”记录成 Intent。

生成型的执行策略：

```text
默认          执行 seed，记录 Agent 行为链，再生成 Intent
generateOnly  不执行 seed，只生成 Intent
agentEngaged  让 Agent 制定 Intent，然后执行生成的 Intent
```

`agentEngaged` 与 `generateOnly` 不冲突：

```text
generateOnly + agentEngaged
  = 让 Agent 制定 Intent，只生成，不执行
```

`skillRequired` 是生成型约束：

```text
必须提供 prompt
必须带 agentEngaged
禁止外部 skill 元数据
生成结果必须包含 skill
```

`save` 与执行无关：

```text
defineOnly    必须 save
generateOnly  必须 save
其他生成路径   save 可选
```

生成完成前不能直接执行；`agentEngaged` 默认生成后执行，叠加 `generateOnly` 才只生成。

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
| `prompt` | 禁止；写在 `Intent.prompt` 内 | 生成 seed，必需 | `asSkill` 外部 prompt；其他模式用内置 |
| `skill` | 禁止；写在 `Intent.skill` 内 | 可选；`skillRequired` 时禁止 | `asSkill`/`withSkill` 可外部覆盖 |
| `steps` / `executor` | 在 `Intent` 内 | 禁止 | 禁止（来自已存 Intent） |
| `save` | 可选；不写不落盘 | 默认生成可选；`generateOnly` 必需 | 禁止 |
| `defineOnly` | 可选：只定义 | 禁止 | 禁止 |
| `generateOnly` | 禁止 | 可选：只生成，不执行 seed | 禁止 |
| `agentEngaged` | 禁止 | 可选：生成后执行；叠加 `generateOnly` 则只生成 | 禁止 |
| `skillRequired` | 禁止 | 可选约束：必须 `prompt + agentEngaged`，禁止外部 `skill` | 禁止 |
| `asPrompt` | 禁止 | 禁止 | 可选 |
| `asSkill` | 禁止 | 禁止 | 可选 |
| `withSkill` | 禁止 | 禁止 | 可选 |
| 参数 | 定义参数 / 执行参数 | 生成 seed 参数 | 调用参数 |
| 调用方 | 用户 / Agent | 用户 / 宿主；Agent 不能调用 | 用户 / Agent |

#### 必需 / 禁止关系

| 模式 / 标签 | 必需 | 禁止 |
|---|---|---|
| `asPrompt` | `Intent.prompt` | 外部 `prompt` / `skill` |
| `asSkill` | 外部 `prompt` | 外部 `Intent` |
| `withSkill` | `Intent.prompt` + `Intent.skill` | 外部 `prompt` |
| `asPrompt + withSkill` | `Intent.prompt` + `Intent.skill` | 外部 `prompt` |
| `asPrompt + withSkill + skill` | `Intent.prompt` + 外部 `skill` 覆盖 | 外部 `prompt` |
| `skillRequired` | `prompt` + `agentEngaged` | 外部 `skill` |
| `generateOnly` | `prompt` + `save` | `Intent` / 执行标签 |
| `agentEngaged` | `prompt` | `Intent` / 执行标签 |
| `defineOnly` | `Intent` | 执行标签 / 生成标签 |
| `Intent` 定义 | 完整 `Intent` | 顶层 `prompt` / `skill` |

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
不接受外部 prompt
单独使用时也不接受外部 skill
和 withSkill 组合时例外：见 4.5
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
只使用 Intent 内置 prompt
默认使用 Intent 内置 skill
不接受外部 prompt
可接受外部 skill 覆盖内置 skill
不执行 steps / executor
```

要求：

```text
Intent.prompt 非空
Intent.skill 或外部 skill 非空
```

### 4.5 `asPrompt + withSkill`

```text
Intent.prompt + Intent.skill
不执行 steps / executor
```

允许：

```text
Intent.prompt 内置
Intent.skill 内置
外部 skill 覆盖内置 skill
```

禁止：

```text
外部 prompt
```

示例：

```text
/it demo.explain2 {topic:"deepseek"} -> {
  skill:"用更口语的方式说明。",
  tags:["asPrompt","withSkill"]
}
```

实际发送：

```text
<Intent.prompt>

[Skill]
<外部 skill 或 Intent.skill>
```

### 4.6 核心区别

| 标签组合 | prompt | skill |
|---|---|---|
| `asPrompt` | 内置 | 忽略 |
| `asSkill` | **必须外部** | 外部优先，否则内置 |
| `withSkill` | 内置 | 内置；外部可覆盖 |
| `asPrompt + withSkill` | 内置 | 内置；外部可覆盖 |

## 5. 标签冲突

### 5.0 完整冲突矩阵

图例：

```text
✓定   定义型：有完整 Intent / 直接定义
✓生   生成型：由元数据 + Agent 行为链生成
✓执   执行型：消费已有 Intent
✓生/执 同一组合可作为生成型或执行型
✓定/生 同一组合在“直接定义”和“记录行为生成”两种上下文下都成立
△无   可解析，但被忽略/不构成完整类型
✗     拒绝
—     自身/不适用；重复标签本身也会被拒绝
↔     与上三角对称
规    规划中的能力，当前代码未实现
```

| 行 \ 列 | prompt | skill | Intent | asPrompt | asSkill | withSkill | save | defineOnly | generateOnly | agentEngaged | skillRequired |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **prompt** | — | ✓ 生/执<br>生需 save | ✗ 定义禁 prompt | ✗ 禁外置 | ✓ 执<br>prompt 必 | ✗ 禁外置<br>仅 prompt-only 可省略 asPrompt | ✓ 生<br>save 必 | ✗ 需 Intent | ✓ 生<br>prompt 必 / save 必 | ✓ 生<br>save 可 | ✓ 生<br>AE 必 |
| **skill** | ↔ | — | ✗ 定义禁 skill | ✗ 单用禁<br>+withSkill 可 | ✓ 执<br>skill 可 | ✓ 执<br>skill 可 | △ 无<br>需 prompt | ✗ 需 Intent | ✓ 生<br>需 prompt/save 必 | ✓ 生<br>需 prompt | ✗ 禁 skill |
| **Intent** | ↔ | ↔ | — | ✗ 定义 + 执行冲突 | ✗ 定义 + 执行冲突 | ✗ 定义 + 执行冲突 | ✓ 定<br>save 可 | ✓ 定<br>save 必 | ✗ 定义 + 生成冲突 | ✗ 定义 + 生成冲突 | ✗ 定义 + 生成冲突 |
| **asPrompt** | ↔ | ↔ | ↔ | — | ✗ 与 asSkill 冲突 | ✓ 执<br>skill 可覆盖 | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 |
| **asSkill** | ↔ | ↔ | ↔ | ↔ | — | ✗ 与 withSkill 冲突 | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 |
| **withSkill** | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✗ save 冲突 | ✗ defineOnly 冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 | ✗ 生成 + 执行冲突 |
| **save** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✓ 定<br>save 必 | ✓ 生<br>save 必 | ✓ 生<br>save 可 | ✓ 生<br>AE 必 |
| **defineOnly** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✗ 定义 + 生成冲突 | ✗ 定义 + 生成冲突 | ✗ 定义 + 生成冲突 |
| **generateOnly** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✓ 生<br>save 必 | ✓ 生<br>save 必 / AE 必 |
| **agentEngaged** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — | ✓ 生<br>prompt 必 |
| **skillRequired** | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | ↔ | — |

> `AE` = `agentEngaged`；`必` = 该元数据/标签是此组合的必需项。
> `withSkill` 单独用于 Native/Chain Intent 时，必须叠加 `asPrompt`；只有没有内置工具链、默认走 prompt 的 Intent 才能省略 `asPrompt`。
> `generateOnly` 和 `skillRequired` 仍是规范设计，当前代码未实现。

### 5.1 矩阵说明

```text
prompt + skill
  有已存在 Intent + asSkill -> 执行型
  没有 Intent + save -> 生成型
  不再作为“定义型”：定义必须有 Intent 元数据。

prompt + Intent
  拒绝：定义型不允许顶层 prompt/skill。
  定义内容必须写在 Intent.prompt / Intent.skill 内。

skill + Intent
  拒绝：定义型不允许顶层 prompt/skill。

skill + save
  单独 skill 没有 prompt，不能生成/保存。
  会退化为普通 Prompt fallback，skill 被忽略。

prompt + generateOnly
  生成型：prompt 是 seed，不执行 seed，只生成 Intent。
  必须 save。

prompt + agentEngaged
  生成型：Agent 制定 Intent，然后执行生成的 Intent。
  叠加 generateOnly 时只生成、不执行。
  save 可选。

save + prompt
  生成型，不是定义型。
  只有带 Intent 元数据才是定义型。

skillRequired
  必须 prompt + agentEngaged。
  禁止外部 skill，因为 skill 要由 Agent 生成。
```

### 5.2 单字段类型速查

| 单独出现 | 类型 | 含义 |
|---|---|---|
| `Intent` | 定 | 直接硬写完整定义 |
| `prompt` | 生 / 执 | 生成 seed；或执行时的外部 prompt |
| `skill` | 生 / 执 | 生成 seed；或执行时的外部 skill |
| `asPrompt` | 执 | 内置 prompt |
| `asSkill` | 执 | 外部 prompt + skill |
| `withSkill` | 执 | 内置 prompt + 内置 skill；外部 skill 可覆盖 |
| `asPrompt + withSkill` | 执 | 内置 prompt + 内置 skill；外部 skill 可覆盖 |
| `defineOnly` | 定 | 只定义；save 可选 |
| `generateOnly` | 生 | 只生成（规划） |
| `agentEngaged` | 生 | Agent 制定 Intent，不执行（规划） |
| `skillRequired` | 生 | 必须 prompt + agentEngaged；禁止外部 skill（规划） |
| `save` | 修饰 | 保存定义/生成结果 |

### 执行标签互斥

```text
asPrompt + asSkill     拒绝
asSkill + withSkill    拒绝
```

允许：

```text
asPrompt + withSkill
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
Intent + prompt        拒绝
Intent + skill         拒绝
Intent + asPrompt      拒绝
Intent + asSkill       拒绝
Intent + withSkill     拒绝
```

### defineOnly

```text
defineOnly 必须有 Intent:{...}
defineOnly + save 可选；不带 save 只定义在内存，不落盘
defineOnly + asPrompt      拒绝
defineOnly + asSkill       拒绝
defineOnly + withSkill     拒绝
defineOnly + agentEngaged  拒绝
defineOnly + skillRequired 拒绝
```

### 生成型

```text
agentEngaged 需要 prompt；默认生成后执行
agentEngaged + generateOnly 只生成，不执行
generateOnly 需要 prompt + save
skillRequired 需要 prompt + agentEngaged
skillRequired 禁止外部 skill
skillRequired 叠加 generateOnly 时才必须 save
生成型不能和 Intent 定义块同用
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
→ addIntent() 内存安装
→ defineOnly 必须带 save
→ 写 intents.json
→ 不执行
→ 不发给 Agent
```

`save` 规则：

```text
defineOnly + save      定义并落盘
defineOnly 不带 save   拒绝（没有可消费结果）
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
→ addIntent() 内存安装
→ 有 save 才写 intents.json
→ 执行
```

限制：

```text
只允许 Native / Chain
Prompt-only 拒绝
不能叠加顶层 prompt / skill
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

## 8. 生成型：prompt + save

没有 `Intent` 元数据，只有 `prompt` / `skill` 和 `save` 时，属于**生成型**，不是定义型。

```text
/it demo.explain2 {topic:"deepseek"} -> {
  prompt:"解释 [topic]。",
  skill:"先给出定义，再给一个例子。",
  tags:["save"]
}
```

目标语义：

```text
名称 demo.explain2 不存在
→ 没有 Intent 元数据
→ 识别为生成型
→ 执行 seed，或按 generateOnly 不执行
→ 记录 Agent 行为链
→ 生成 Intent 定义
→ save 落盘
→ 生成完成前不能作为执行型
```

生成策略：

```text
默认              执行 seed，记录行为链，生成并保存
generateOnly      不执行 seed，只生成
agentEngaged      Agent 制定 Intent，不执行
skillRequired     必须由 Agent 生成 skill；禁止外部 skill
```

生成结果至少包含：

```text
name
prompt
可选 skill
由行为链生成的 steps
```

当前实现会生成 `name + prompt + skill`，并把本轮 Agent 的 IntentCall 固化为 `steps`。  
`skillRequired` 会调用模型生成 skill，不再直接报错。

生成完成后，它才变成定义型/执行型：

```text
/it demo.explain2 {topic:"deepseek"} -> {tags:["asPrompt"]}
/it demo.explain2 {topic:"deepseek"} -> {
  prompt:"用中文解释 [topic]。",
  tags:["asSkill"]
}
/it demo.explain2 {topic:"deepseek"} -> {
  skill:"用更口语的方式说明。",
  tags:["withSkill"]
}
```

注意：

```text
定义型 = 有 Intent 元数据
生成型 = 没有 Intent 元数据，通过 prompt/skill + 行为链生成
执行型 = Intent 已存在
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
  让 Agent 制定 Intent，不执行
  生成型的可选策略

skillRequired
  是 agentEngaged 的约束
  必须提供 prompt
  禁止外部 skill
  生成结果必须包含非空 skill
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
生成型骨架已实现：prompt/skill -> Intent，行为链 -> steps
generateOnly / agentEngaged 已实现“不执行、只生成”
skillRequired 已接入模型生成 skill
save + prompt 已走生成型，不再创建旧 PromptIntent
Intent 定义块已按 save 决定是否落盘
asPrompt + withSkill 已允许
withSkill 已允许外部 skill 覆盖
withSkill 在 chain/native 上强制 asPrompt
skillRequired 已禁止外部 skill，并要求 prompt
Prompt-only Intent 已进入模型工具表
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
asPrompt            内置 prompt，忽略 skill
asSkill             外部 prompt 必须，skill 可选
withSkill           内置 prompt + 内置 skill；外部 skill 可覆盖
asPrompt + withSkill 内置 prompt + 内置 skill；外部 skill 可覆盖
save + prompt       生成型，不是定义型
定义型               必须有 Intent 元数据
生成型               没有 Intent，靠 prompt/skill + Agent 行为链
执行型               Intent 已存在
skillRequired       必须 prompt + agentEngaged；禁止外部 skill
```

```text
用户 /it 和 Agent tool call 都生成 IntentInvocation
Agent 直接调用 Intent；定义只是同一个 IntentInvocation 携带 Intent
```
