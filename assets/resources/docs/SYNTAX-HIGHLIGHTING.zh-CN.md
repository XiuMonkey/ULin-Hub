# 语法高亮：官方怎么做、本版怎么做、怎么加语言

> 结论放在最前：**官方用的是 Shiki（VS Code 的 TextMate 语法），不是自己写规则、也不是"直接解析"。**
> 本版是"等价目标、不同手段"：一份**手写正则规则表 + 单层贪心扫描器**，因为客户端是 Qt/C++，
> 没有 JS 运行时可以背 Shiki。本文记录官方实现的事实（从 `app.asar` 读出来的），
> 本版的规则格式/引擎/限制，以及加语言的流程。

---

## 1. 官方实现（DSH 官方版客户端）

代码位置：`app.asar` → `dsh/node_modules/@deepseek-ai/dsh-client-ui-primitives/lib/index.js`
（约 12383 行，其中 `lib/types/markdown/highlight.js` 一段是高亮器本体）。

### 1.1 引擎与语法来源

```js
import { createCssVariablesTheme, createHighlighterCoreSync } from "shiki/core";
import { createJavaScriptRegexEngine } from "shiki/engine/javascript";
import langTs   from "@shikijs/langs/typescript";
import langBash from "@shikijs/langs/shellscript";
import langJson from "@shikijs/langs/json";
```

- **Shiki** 的 fine-grained core，**同步**构造（`createHighlighterCoreSync`）。
- **语法 = TextMate 语法**：`@shikijs/langs/<id>` 的默认导出是 `LanguageRegistration[]`，
  内容就是 VS Code 的 `.tmLanguage.json`（`match` / `begin`–`end` 状态机 + `include` 嵌套子语法）。
  所以"规则从哪来"的答案是：**从 VS Code 生态搬运，不是自己写**。
- **正则引擎**：`createJavaScriptRegexEngine({ forgiving: true, regexConstructor: … lazyCompileLength: Infinity })`
  —— 用 JS 原生 RegExp，**不背 Oniguruma WASM**（bundle 友好），并把 Shiki 默认的"超 3000 字符延迟编译"
  关掉（`Infinity`）：官方注释说否则会在 Shiki 每行 500ms 的预算下返回**部分 token 流**。

### 1.2 语言集：3 个启动即载 + 50 多个按需

```js
const LANGS = [langTs, langBash, langJson];          // 启动即载：markdown 围栏 + run_code 最常用的三个
const LAZY_GRAMMARS = new Map([
  ["python", () => import("@shikijs/langs/python")],
  ["rust",   () => import("@shikijs/langs/rust")],
  … // 共 57 个：go/java/c/cpp/csharp/kotlin/swift/php/yaml/toml/ini/markdown/mdx/html/css/scss/less/
    // sql/xml/lua/bat/powershell/fish/dotenv/log/csv/diff/http/rst/latex/bibtex/asciidoc/r/julia/dart/
    // scala/clojure/erlang/elixir/haskell/fsharp/vb/perl/verilog/system-verilog/graphql/proto/hcl/nix/
    // vue/svelte/make/cmake/groovy
]);
```

- 首次渲染某个 lazy 语言时先出**纯文本**，语法模块载入后通过 `subscribeGrammarLoaded` /
  `grammarLoadCount`（`useSyncExternalStore` 那套）通知组件**重渲染**成高亮版。
- JS 家族（`js/jsx/ts/tsx`）全部映射到 **TypeScript 语法**（官方明说是 trade-off：JSX/TSX 只是近似）。
- **不认识的语言 → 纯文本（仍等宽），不报错**。

### 1.3 别名表

`LANG_ALIASES`（约 120 条 `Map`）：fence 标签、文件扩展名、`readLangHintForPath` 的短 id 都归一到这里。

```js
["ts","typescript"], ["py","python"], ["yml","yaml"], ["sh","shellscript"],
["ps1","powershell"], ["tf","hcl"], ["proto","proto"], ["gradle","groovy"] …
```
官方特意写成 `Map` 而不是对象：fence 里的字符串是模型随手写的，`constructor` / `__proto__`
落到对象上会命中原型链，`shiki` 内部就炸了。

### 1.4 主题：不绑具体配色，走 CSS 变量

```js
const cssVariablesTheme = createCssVariablesTheme({
  name: "css-variables", variablePrefix: "--shiki-", fontStyle: true
});
```

于是 token 输出的是 `<span style="color:var(--shiki-token-keyword)">`，**颜色值全在主题包的 CSS 里**
（`--shiki-token-constant/string/comment/keyword/parameter/function/string-expression/punctuation/link`，
light + dark 两套）。本版把这 9 组色按 light/dark 取中间值落进了规则表（见 §2.3）。

---

## 2. 本版实现

### 2.1 结构

| 文件 | 作用 |
| --- | --- |
| `assets/resources/config/highlight-rules.json` | **生成产物**（39 语言 / 377 规则 / 134 别名 / 54 KB），编进 qrc：`:/ULinHub/config/highlight-rules.json`。**规则只写角色，不写颜色** |
| `assets/resources/styles/theme-{light,dark}.json` | **颜色的唯一来源**：`syntaxKeyword / syntaxString / syntaxComment / syntaxConstant / syntaxFunction / syntaxVariable / syntaxPunct` 七个键，亮/暗各一套 |
| `misc/tools/gen-highlight-rules.js` | **规则生成器**（改词表只改这里，再重跑） |
| `source/src/common/util/CodeHighlighter.cpp` | 引擎：加载规则 + 别名归一 + 角色→颜色（按当前主题）+ 贪心扫描 + 缓存 |
| `source/src/common/session/MessageListModel.cpp` | `invalidateSegmentParts()`：换肤后让代码块按新配色重建 |
| `source/src/qmlbridge/ThemeBridge.cpp` | 主题装载/切换时把 `syntax*` 推给高亮器（`setThemeColors`） |
| `source/src/common/util/MarkdownSegmenter.cpp` | 把 markdown 切成"正文 / 代码块"，取围栏标签交给高亮器 |

### 2.2 引擎（`CodeHighlighter::highlight`）

1. **归一语言**：`trim` → 取第一个词（` ```js title=x ` → `js`）→ 去掉 ``` ` ' " { } ``` 装饰 → 小写 → 查别名表；
2. 查 `languages[lang]`，查不到用 `languages.default`；
3. **贪心扫描**：从 `pos` 起，在**该语言的每条规则**上跑 `QRegularExpression::match(code, pos)`，
   取"**最早命中**、同位置**最长者胜**"的一条；未命中区间原样 HTML 转义，命中段套
   `<span style="color:#xxxxxx">`；
4. 结果按 `lang + "\n" + code` 缓存（流式渲染时同一段代码会重复问）。

这条算法对"文本里谁先开始"天然正确：字符串/注释一定比它们内部的 keyword 先开始，所以不会串色。
零宽匹配在 C++ 侧显式跳过（`matchLen <= 0 → continue`），不会原地死循环。

### 2.3 配色：亮/暗分开，且由主题包决定

规则里写的是**角色**，色值在主题包里（`theme-light.json` / `theme-dark.json`）。
所以：① 亮暗各一套；② 风格包可以直接覆盖；③ 改颜色不用碰规则表。

| 角色 | 主题键 | light | dark | 官方来源 |
| --- | --- | --- | --- | --- |
| `keyword` | `syntaxKeyword` | `#D6336C` | `#FAA2C1` | `--shiki-token-keyword` |
| `string` | `syntaxString` | `#237032` | `#69DB7C` | `--shiki-token-string`（亮色按用户要求比官方 `#2F9E44` 再深一档） |
| `comment` | `syntaxComment` | `#868E96` | `#ADB5BD` | `--shiki-token-comment` |
| `constant`（数字/常量/类型） | `syntaxConstant` | `#1C7ED6` | `#4DABF7` | `--shiki-token-constant` |
| `function`（函数/调用/内建） | `syntaxFunction` | `#6741D9` | `#B197FC` | `--shiki-token-function` |
| `variable`（变量/成员/键名/`$VAR`） | `syntaxVariable` | `#E8590C` | `#FFA94D` | `--shiki-token-parameter` |
| `punct`（行首记号/列表符） | `syntaxPunct` | `#495057` | `#CED4DA` | `--shiki-token-punctuation` |

装饰器 / 注解 / 预处理指令 / 指令（`@dataclass`、`#include`、`-Parameter`）归 `keyword`：
官方没有独立的装饰器色，而 amber 与 `variable` 的橙太近（`#DD8629` vs `#E8590C`），拆开反而分不清。
内建符号（`print` / `len` / `console`）归 `function` —— 官方 python 语法里它们本来就是 `support.function`。

渲染链路：

```
MarkdownSegmenter::split()            切出代码块，问高亮器要 html
   └─ CodeHighlighter::highlight()    规则命中的角色 → m_themeColors[role] → <span style="color:#…">
ThemeBridge::rebuild()                主题装载/切换时 setThemeColors(当前调色板, 是否暗色)
   └─ 清缓存；换肤后 QmlBridge 把 ThemeBridge::changed 接到
      MessageListModel::invalidateSegmentParts()，让已渲染的代码块重建
```

没有主题色时**不着色**（返回纯文本），而不是烘一个兜底色 —— 暗色下兜底色可能直接不可读。

> ⚠️ **分布上的坑（本次踩过，务必记住）**：主题包实际读的是 `<exe>/ui/theme-*.json`（磁盘优先，qrc 兜底）。
> 只要那个副本被手工改过，`ThemeManager::ensureStylePack()` 就把它当"用户已改"、**不再覆盖** ——
> 于是"源码加了 `syntax*` 键、exe 里的副本还是旧的"，运行时角色查不到色，整片代码块就退化成纯文本
> （用户看到的就是"高亮直接没了"）。
> 处理：删掉 `<exe>/ui/theme-*.json`，下次启动会从 qrc 重新释放；开发期若手工同步过副本，改完记得再同步一次。
> 另外引擎里保留了一张**内置兜底表**（`CodeHighlighter.cpp` 的 `kFallbackColors`）：主题包缺键时按亮/暗退到官方色，
> 保证"高亮永远不消失"，最坏只是配色不是最新的。

### 2.4 支持的语言（39）

```
typescript（js/ts/tsx/jsx 共用）  python  shellscript（bash/sh/zsh）  powershell
json  yaml  toml  ini（properties/dotenv/env）  sql  html（vue/svelte）
xml（svg）  css  scss  less  markdown（md/mdx）  diff（patch）
go  rust  java  kotlin  c  cpp  csharp  php  ruby  lua  r  dart  scala  swift
dockerfile  make  cmake  proto  graphql  hcl（tf）  http  bat（batch/cmd）  default
```

别名共 134 条，与官方 `LANG_ALIASES` 对齐（`py`/`js`/`c++`/`yml`/`ps1`/`tf`/`gql`/`gradle`… 都能落位）。

### 2.5 已知限制（与官方的差距）

| 差距 | 官方（Shiki） | 本版 | 影响 |
| --- | --- | --- | --- |
| 语法模型 | TextMate `begin/end` **状态机** | 单层"最早最长"贪心 | 嵌套结构只能近似：heredoc、`#if 0` 块注释、模板字符串里再嵌 `${}` 的细语法、markdown 里的围栏内嵌 |
| 子语法嵌套 | `embeddedLanguages`（HTML 里的 `<script>` 走 JS） | 无 | 混合文件（vue/svelte/html+script）只按外层规则 |
| 语言数 | 57 个 lazy grammar | 39 个 | 冷门语言仍是 `default` 兜底 |
| 增量/流式 | token 缓存 + 每行预算 | 按 `lang+code` 缓存全段 | 流式长块每帧重算一次（实测单段 < 5 ms） |

要做成"真状态机"，得给规则格式加 `begin/end/while` 作用域 + 在 C++ 里写栈式扫描器 —— 大约是一次
`CodeHighlighter` 的 3~4 倍工作量，收益集中在上述少数结构上。**目前判断不值得**，除非你确实常看这几种文件。

---

## 3. 加一门语言（三步）

```powershell
# ① 改词表：misc/tools/gen-highlight-rules.js 里加一个 def('mylang', () => […rules])
#    或者复用 cFamily('mylang', { preprocessor, call, member, consts, types, kws, builtins })
#    （同时把 fence 别名加进 ALIASES）
$node = 'assets\resources\server\node.exe'
& $node misc\tools\gen-highlight-rules.js          # ② 重新生成 JSON
```
```powershell
# ③ 自检（会验证所有规则能被 PCRE2 编译 + 别名归一 + 若干回归点）
powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\cpp-selfcheck\run.ps1
# ④ 重编（rules JSON 编进 qrc，改完必须重编才进内置资源）
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
    'source\ULin Hub.vcxproj' /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

### 写规则的约定（踩过的坑）

1. **不要用 `^`**：`QRegularExpression` 默认不是 multiline，`^` 只匹配**整串开头**（流式代码里几乎永远不命中）。
   需要行首语义就用 `(?:^|\n)`，代价是那个换行符被一起包进 span（不可见，无害）。
2. **不要写零宽规则**：引擎会跳过，但会白跑；宁可多写一个字面量。
3. **避免嵌套量词**（`(a+)+` 这类）：代码是流式到达的，每帧都会在"半截代码"上重跑。
   自检里的病态输入（4000 字符 + 各种引号）逐语言计时，目前最慢 css 18 ms。
4. **字符串/注释规则要能被"更早出现"覆盖**：贪心算法靠"起点更早"保证不串色，不要试图用 `^` 或后行断言去限定。
5. `<...>` 这类宽泛模式必须绑死在它该在的上下文里（例：`#include <vector>` 整条吃，
   单写 `<[A-Za-z0-9_.]+>` 会把 `std::vector<int>` 的模板实参染成字符串 —— 本次踩过，已加回归断言）。

---

## 4. 验证手段

| 手段 | 位置 | 覆盖 |
| --- | --- | --- |
| C++ 自检（QtCore） | `misc/tools/cpp-selfcheck/`（第 ④ 节） | 规则文件装载、别名归一（`py`/`PY`/`c++`/`js`/带附加信息的 fence）、`#include` 回归、未知语言兜底、HTML 转义、**377 条规则全部能被 PCRE2 编译** |
| 规则自检（Node） | `build/temp/official-ui/hl-harness.js` | 用与 C++ 同构的算法复跑 18 个语言的样例并输出 ANSI 彩色结果；病态输入逐语言计时；空匹配/无效正则体检 |
