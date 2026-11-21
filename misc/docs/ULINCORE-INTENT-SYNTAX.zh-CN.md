# ULinCore Intent / `/it` 规格

> 状态：参数冒号、`skill`、`hasSkill/AsSkill` 已有基础实现；
> `tags`、`Intent` 定义块、`defineOnly`、Agent 工具仍在实现中。

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
- 参数值先保存**原文**，绑定层再按 `parameters` schema 强转。
- `Intent:{...}` 必须按结构化 JSON/JSON5 解析。

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

只有两大类：

```text
非布尔值：prompt / skill / Intent
布尔状态：tags[]
```

### 3.1 非布尔键值对

```json5
{
  prompt: "用 [style] 解释 [topic]",
  skill: "先确认读者，再按定义、机制、常见误区组织。",
  Intent: { /* 完整定义 */ }
}
```

### 3.2 单数组标签

```json5
{
  tags: ["asPrompt", "save", "agentEngaged", "skillRequired"]
}
```

标签只写“偏离默认”的状态；默认值不写标签。

| 维度 | 默认 | 非默认标签 |
|---|---|---|
| exec | `auto`：Native / Chain / Prompt | `asPrompt` |
| exec | 同上 | `asSkill` |
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

只有一组内部可能互斥：

```text
asPrompt / asSkill
```

其他维度都是“写标签 = 打开，不写 = 默认关闭”。

### 3.3 标签命名原则

先定“族”，再定具体状态：

```text
exec:   as*
save:   save
author: agent*
skill:  skill*
define: define*
```

规则：

1. 同一维度使用统一前缀/词根。
2. 需要正反义时，两侧结构尽量一致。
3. 不混用动词、名词、形容词的随意组合。
4. 不新增裸标签；新标签必须能归入已有族。
5. 标签只表达状态，不表达 `isX` 布尔字段。

需要成对命名时，用同一结构：

```text
save / noSave
agentEngaged / agentDisengaged
skillRequired / skillOptional
asPrompt / asSkill / asAuto
```

避免：

```text
save / skipSave                 结构不一致
hasSkill / skillOptional        一个动词短语，一个状态
defineOnly / defineAndExecute   长度和语义结构不一致
auto / asPrompt                 一个裸词，一个族内标签
```

当前只保留非默认标签，所以不需要真的写出所有反义标签；但新增标签时仍按上述规则命名。

## 4. 标签冲突

### 跨维度拒绝

```text
save + asPrompt
save + asSkill
defineOnly + asPrompt
defineOnly + asSkill
Intent + asPrompt
Intent + asSkill
defineOnly + agentEngaged
defineOnly + skillRequired
Intent + agentEngaged
Intent + skillRequired
agentEngaged 没有 save
skillRequired 没有 agentEngaged
```

### 标签与键值对

```text
Intent + 无 define 标签          默认 defineAndRun
Intent + defineOnly              只定义/保存，不执行
Intent + as*                     拒绝
Intent 只有 prompt                拒绝 defineAndRun
defineOnly 没有 Intent           拒绝（空定义）
defineOnly + prompt              拒绝（定义写 Intent.prompt）
defineOnly 没写 save             拒绝（只有定义模式才保存）
agentEngaged + Intent            拒绝
skillRequired + Intent           拒绝
```

### 执行期与保存期

```text
as* 标签不能和 save 同用
as* 标签不能和 Intent 定义块同用
defineOnly 不能和 as* 同用
```

## 5. 执行方式

### 已有 Intent，裸执行

```text
executor 非空 -> Native
steps 非空   -> Chain
prompt 非空  -> Prompt
```

### `asPrompt`

```text
只发 prompt
忽略 skill / steps / executor
```

要求 prompt 非空。

### `asSkill`

```text
prompt + skill
skill 只补充方法，不替代用户意图
不执行 steps / executor
```

要求 prompt 和 skill 都非空。

## 6. 只定义

```text
/it summarize -> {
  Intent:{
    description:"解释主题",
    prompt:"用 [style] 解释 [topic]",
    skill:"",
    parameters:[
      { name:"topic", type:"string" },
      { name:"style", type:"string", default:"简洁" }
    ],
    steps:[],
    executor:""
  },
  tags:["defineOnly", "save"]
}
```

语义：

```text
校验 Intent
→ 安装
→ 保存
→ 不执行
→ 不发给 Agent
```

`name` 由 `/it name` 提供，不在 `Intent` 块里重复。  
定义 skill 直接写 `Intent.skill`，不用 `skillRequired`。

## 7. 定义并执行（有 `Intent` 时的默认）

```text
/it summarize -> {
  Intent:{...}
}
```

有 `Intent` 且没有 `defineOnly` 标签时，默认就是 `defineAndRun`。

语义：

```text
校验 Intent
→ 安装
→ 执行
→ 按 save 标签保存或不保存
```

限制：

```text
只允许 Native / Chain
Prompt-only 拒绝
不能叠加 as* / agentEngaged / skillRequired
```

Prompt-only 拒绝的原因：

```text
定义并执行 prompt-only Intent
→ 需要再起一个模型回合
→ 当前回合没有明确接收者
→ 递归 / 重复上下文
```

Prompt-only 应该保存定义，之后由用户执行；Agent 调用同一 Intent 时只拿渲染结果，不递归起模型回合。

## 8. 用户与 Agent

只有一个 IntentRegistry、一个 `IntentInvocation`、一个 dispatch。

```text
用户：/it name {args}
Agent：tool call { name, arguments }
        ↓
   IntentInvocation
        ↓
resolveIntent → 参数绑定 → 执行/渲染
```

### 用户

```text
/it name {args}                         执行已有 Intent
/it name {args} -> {tags:["asPrompt"]}  只执行 prompt
/it name {args} -> {tags:["asSkill"]}   执行 prompt + skill
/it name {args} -> {tags:["save"]}      执行并保存
/it name -> {Intent:{...}, tags:["defineOnly","save"]}   只定义
/it name -> {Intent:{...}}                              定义并执行（默认）
```

### Agent

Agent 直接调用 Intent，不使用 `intent_execute` / `intent_render` / `intent_upsert` / `intent_define` 这组包装。

普通执行：

```json
{ "name": "read_and_search", "arguments": { "path": "/tmp/a", "keyword": "QML" } }
```

Prompt/Skill 执行：

```json
{
  "name": "summarize",
  "arguments": { "topic": "QML" },
  "metadata": { "tags": ["asSkill"] }
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

工具协议由 ULin 自己实现，直接支持 `metadata`，不需要 `__intent` 保留字段，也不需要把元数据塞进 arguments。

```text
IntentInvocation {
  name,
  arguments,
  tags,
  Intent
}
```

结果形态由 Intent 能力决定：

```text
Native  -> 执行结果
Chain   -> 链执行结果
Prompt  -> 渲染后的 prompt
Skill   -> 渲染后的 prompt + skill
```

不需要 `intent_define`。定义只是同一个 `IntentInvocation` 携带了 `Intent`。  
协议允许 `name` 尚未注册但携带 `Intent` 定义的情况；由 dispatcher 校验、安装，再按 tags 决定执行或只保存。

### 区别

```text
用户：文本 /it 入口
Agent：结构化 tool call 入口

用户：Prompt/Skill 结果交给宿主模型回合
Agent：Prompt/Skill 结果作为工具结果自己使用

权限：Agent 写定义/执行高危 executor 需要额外授权
```

不是：

```text
用户一套解析
Agent 一套解析
```

Agent 不允许：

```text
使用 intent_execute / intent_render / intent_upsert / intent_define 另一套 API
替用户发起 agentEngaged / skillRequired 泛化
绕过 allowWrites / allowShell
覆盖内置 Intent
```

Agent 定义并执行的前提：

```text
Intent 完整、可校验
只能执行 Native/Chain
不能创建未知 executor
不能覆盖内置 Intent
```

## 9. agentEngaged / skillRequired

```text
agentEngaged
  执行成功后允许 Agent 设计处理方案
  可优化 prompt、加 skill、构造 steps、绑定已有 executor，或组合

skillRequired
  是 agentEngaged 的约束
  要求方案必须包含非空 skill
```

约束：

```text
agentEngaged 必须有 save + prompt
skillRequired 必须有 agentEngaged
二者不能和 Intent 定义块同用
```

当前只有解析/规划，没有自动泛化。

## 10. 当前实现状态

已实现：

```text
/it 参数冒号
skill 字段
hasSkill / AsSkill 解析
AsSkill 执行（prompt + skill）
```

规划中：

```text
tags 单数组
Intent 定义块
defineOnly（有 Intent 时默认 defineAndRun）
agentEngaged / skillRequired 自动泛化
统一 IntentInvocation（用户 /it 与 Agent tool call）
Prompt/Skill Intent 的渲染结果
Agent tool call 携带 tags / Intent 元数据
```

## 11. 最小记忆

```text
/it name {参数} -> {键值对 + tags} tail
```

```text
参数    原文进入，绑定层强转
键值对  prompt / skill / Intent
tags    布尔状态，一个数组
调用    用户 /it 和 Agent tool call 都生成 IntentInvocation
Agent   直接调用 Intent；定义只是同一个 IntentInvocation 携带 Intent
```
