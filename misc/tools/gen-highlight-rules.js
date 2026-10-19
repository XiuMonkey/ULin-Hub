// 生成 assets/resources/config/highlight-rules.json（语法高亮规则）。
//
// 为什么用生成器而不是手改 JSON：
//   * 规则结构高度重复（注释 / 字符串 / 常量 / 关键字 / 类型 / 内置 / 函数调用 / 成员 / 数字），
//     每个语言只是"词表不同"；手写 40 个语言 × 12 条要改 500 次，生成器只写词表。
//   * 配色必须统一走官方 Shiki 调色板（见 PALETTE），生成器保证没有漏网的旧色。
//   * 引擎是"最早匹配、同位置最长者胜"的单层贪心扫描（见 CodeHighlighter.cpp），
//     规则的**相对顺序不影响结果**（只影响同起点时的长度比较），所以按类别排序即可。
//
// 用法（仓库自带 node，不用另装）：
//   assets\resources\server\node.exe misc\tools\gen-highlight-rules.js
// 产物：assets/resources/config/highlight-rules.json（编进 qrc，改完要重编）
//
// ⚠️ 正则用 PCRE2 语法（QRegularExpression）。避免 `^`（默认不 multiline，只会命中整串开头），
//    需要行首语义时用 `(?:^|\n)`。也避免任何可能导致灾难性回溯的嵌套量词 —— 代码是流式到达的，
//    规则每帧都会在"半截代码"上重跑一次。

const fs = require('fs');
const path = require('path');

// ── 角色（不是颜色！）────────────────────────────────────────────────────────
// 规则里只写"这一段是什么"（角色），具体色值由**主题包**给：
//   theme-light.json / theme-dark.json 的 syntaxKeyword / syntaxString / … 八个键。
// 这样亮暗各一套、风格包可覆盖、换肤立刻生效，规则文件本身与配色完全解耦。
//
// 角色 -> 官方 Shiki token（本版取的值见主题包，来源见右侧注释）：
//   keyword  --shiki-token-keyword        （light #d6336c / dark #faa2c1）
//   string   --shiki-token-string         （light #2f9e44 / dark #69db7c）
//   comment  --shiki-token-comment        （light #868e96 / dark #adb5bd）
//   constant --shiki-token-constant（数字/常量/类型；light #1c7ed6 / dark #4dabf7）
//   function --shiki-token-function（函数/调用/内建；light #6741d9 / dark #b197fc）
//   variable --shiki-token-parameter 家族（变量/成员/键名；light #e8590c / dark #ffa94d）
//   attr     --dsw-static-amber（装饰器/注解/预处理/指令；light #dd8629 / dark #f7ad31）
//   punct    --shiki-token-punctuation    （行首记号/标点；light #495057 / dark #ced4da）
const PALETTE = {
  keyword: 'keyword',
  string: 'string',
  comment: 'comment',
  constant: 'constant',
  function: 'function',
  variable: 'variable',
  // 装饰器 / 注解 / 预处理 / 指令归到 keyword：官方没有独立的装饰器色，
  // 而 amber 与 variable 的橙太近（light #dd8629 vs #e8590c），拆开反而分不清。
  attr: 'keyword',
  punct: 'punct'
};

// ── 复用片段 ────────────────────────────────────────────────────────────────
const RE = {
  dq: '(?:[^"\\\\\\n]|\\\\.)*',          // 双引号字符串体（不跨行）
  sq: "(?:[^'\\\\\\n]|\\\\.)*",          // 单引号字符串体
  bt: '(?:[^`\\\\]|\\\\[\\s\\S])*',      // 反引号字符串体（可跨行，模板字符串）
  lineComment: (p) => `${p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}[^\\n]*`,
  blockComment: (open, close) =>
    `${open.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}[\\s\\S]*?(?:${close.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}|$)`,
  // 数字：整数 / 小数 / 指数 / 0x 0b 0o / 分隔下划线 / 结尾类型后缀（1.0f, 10n）
  number: '\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+|\\d[\\d_]*(?:\\.\\d[\\d_]*)?(?:[eE][+-]?\\d+)?)(?:[uUlLfFnNiIjJ]+)?\\b',
  // 函数调用：标识符 + 左括号（负向断言排除关键字后面的括号由关键字规则先手覆盖）
  call: '\\b([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()',
  // 成员访问：.name
  member: '(?<=\\.)[A-Za-z_][A-Za-z0-9_]*',
  words: (s) => `\\b(?:${s.trim().split(/\s+/).join('|')})\\b`
};

// 规则里只写角色（keyword/string/…），不写色值：色值由主题包按亮暗分别给
const rule = (pattern, role) => ({ pattern, role });
const kw = (words) => rule(RE.words(words), PALETTE.keyword);
const typeRule = (words) => rule(RE.words(words), PALETTE.constant);
const constRule = (words) => rule(RE.words(words), PALETTE.constant);
const builtinRule = (words) => rule(RE.words(words), PALETTE.function);

// ── 语言定义 ────────────────────────────────────────────────────────────────
// 每个语言给：行注释 / 块注释 / 字符串集合 / 词表 / 额外规则开关。
const LANGS = {};

function def(id, spec) {
  LANGS[id] = spec;
}

// ---- 通用 C 家族骨架 -------------------------------------------------------
function cFamily(id, o) {
  const rules = [];
  if (o.preprocessor) {
    // 整条预处理指令一起吃掉：`<...>` 只在 include 这一行里才是头文件名，
    // 单独一条 `<[A-Za-z0-9_.]+>` 会误伤 `std::vector<int>` 这种模板实参（实测踩过）。
    rules.push(rule('#\\s*include\\s*(?:<[^>\\n]+>|"[^"\\n]+")', PALETTE.attr));
    rules.push(rule('#\\s*(?:define|ifdef|ifndef|endif|pragma|error|warning|undef|include_next|elif|else)\\b[^\\n]*', PALETTE.attr));
  }
  rules.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  rules.push(rule(RE.lineComment('//'), PALETTE.comment));
  if (o.attributes) rules.push(rule(`\\[\\[[^\\]\\n]*\\]\\]|\\b__attribute__\\s*\\(\\([^)\\n]*\\)\\)`, PALETTE.attr));
  rules.push(rule(`"${RE.dq}"`, PALETTE.string));
  rules.push(rule(`'(?:[^'\\\\\\n]|\\\\.)*'`, PALETTE.string));
  if (o.consts) rules.push(constRule(o.consts));
  if (o.types) rules.push(typeRule(o.types));
  if (o.kws) rules.push(kw(o.kws));
  if (o.builtins) rules.push(builtinRule(o.builtins));
  rules.push(rule(RE.number, PALETTE.constant));
  if (o.call) rules.push(rule(RE.call, PALETTE.function));
  if (o.member) rules.push(rule(RE.member, PALETTE.variable));
  return rules;
}

// ---- Python ----------------------------------------------------------------
def('python', () => {
  const r = [];
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule("'''[\\s\\S]*?(?:'''|$)", PALETTE.string));
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('[rbfuRBFU]{1,2}(?="|\')', PALETTE.attr));       // 字符串前缀 r"" f"" b""
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('True False None NotImplemented Ellipsis __name__ __file__ __main__ self cls'));
  r.push(typeRule('int float str bool bytes bytearray list dict set frozenset tuple complex object type range slice memoryview'));
  r.push(kw('and as assert async await break class continue def del elif else except finally for from global if import in is lambda match case nonlocal not or pass raise return try while with yield'));
  r.push(builtinRule('abs all any bin bool callable chr dict dir divmod enumerate eval exec filter float format frozenset getattr globals hasattr hash help hex id input int isinstance issubclass iter len list locals map max min next object oct open ord pow print property range repr reversed round set setattr slice sorted staticmethod str sum super tuple type vars zip'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('(?<=\\.)[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  return r;
});

// ---- TypeScript / JavaScript（官方把 js/ts/tsx/jsx 全映射到 TS 语法）--------
def('typescript', () => {
  const r = [];
  r.push(rule(RE.blockComment('/**', '*/'), PALETTE.comment));  // 先吃掉 jsdoc，避免 /**/ 内层冲突
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('`' + RE.bt + '`', PALETTE.string));              // 模板字符串（可跨行）
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));       // 装饰器
  r.push(constRule('true false null undefined NaN Infinity this super'));
  r.push(typeRule('string number boolean any unknown never void object symbol bigint Array Record Partial Readonly Promise Map Set Date RegExp Error'));
  r.push(kw('abstract as async await break case catch class const constructor continue debugger declare default delete do else enum export extends finally for from function get if implements import in instanceof interface is keyof let namespace new of private protected public readonly return satisfies set static switch throw try type typeof var while yield'));
  r.push(builtinRule('console window document require module exports process globalThis JSON Math Object Array String Number Boolean Promise Symbol Reflect Proxy Map Set WeakMap WeakSet Date RegExp Error parseInt parseFloat isNaN setTimeout setInterval clearTimeout clearInterval fetch'));
  r.push(rule('\\b[A-Za-z_$][A-Za-z0-9_$]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('(?<=\\.)[A-Za-z_$][A-Za-z0-9_$]*', PALETTE.variable));
  return r;
});

// ---- Shell（官方 shellscript：bash/sh/zsh 共用）----------------------------
def('shellscript', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('\\$\\{[^}\\n]*\\}', PALETTE.variable));
  r.push(rule('\\$[A-Za-z_][A-Za-z0-9_]*|\\$[0-9@#?$!*]', PALETTE.variable));
  r.push(rule('(?:^|\\n)\\s*[A-Za-z_][A-Za-z0-9_]*(?==)', PALETTE.variable));   // VAR= 赋值
  r.push(kw('if then else elif fi for while until do done case esac in function return exit break continue local export readonly declare typeset unset shift source alias set trap select time coproc'));
  r.push(builtinRule('echo printf read cd pwd ls cp mv rm mkdir rmdir touch cat head tail grep sed awk find xargs sort uniq wc cut tr chmod chown chgrp ps kill killall sleep test true false env which command type eval exec sudo curl wget tar gzip gunzip ssh scp rsync git node npm pnpm python pip docker make cmake'));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- PowerShell ------------------------------------------------------------
def('powershell', () => {
  const r = [];
  r.push(rule('<#[\\s\\S]*?(?:#>|$)', PALETTE.comment));
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('\\$\\{[^}\\n]*\\}|\\$[A-Za-z_][A-Za-z0-9_:]*', PALETTE.variable));
  r.push(rule('-\\[?[A-Za-z][A-Za-z0-9]*\\]?', PALETTE.attr));    // -Parameter / -[switch]
  r.push(rule('\\[[A-Za-z][A-Za-z0-9_.]*\\]', PALETTE.constant));    // [string] / [System.IO.Path]
  r.push(constRule('$true $false $null $_ $PSItem $args $input $this $HOME $PWD'));
  r.push(kw('begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function if in param process return switch throw trap try until using var while workflow'));
  r.push(builtinRule('Write-Host Write-Output Write-Error Get-Item Get-ChildItem Set-Item Remove-Item New-Item Copy-Item Move-Item Test-Path Join-Path Split-Path Get-Content Set-Content Add-Content Select-Object Where-Object ForEach-Object Sort-Object Measure-Object Get-Process Start-Process Stop-Process Invoke-Expression Invoke-WebRequest Start-Sleep ConvertTo-Json ConvertFrom-Json Out-String Out-File Import-Module Select-String'));
  r.push(rule('\\b[A-Z][A-Za-z0-9]*-[A-Z][A-Za-z0-9]*\\b', PALETTE.function));   // Verb-Noun
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- JSON / JSONC ----------------------------------------------------------
def('json', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('"(?:[^"\\\\\\n]|\\\\.)*"(?=\\s*:)', PALETTE.variable));   // 键
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(constRule('true false null'));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- YAML ------------------------------------------------------------------
def('yaml', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('(?:^|\\n)---[^\\n]*|(?:^|\\n)\\.\\.\\.[^\\n]*', PALETTE.punct));
  r.push(rule('(?:^|\\n)\\s*-\\s', PALETTE.punct));                    // 列表前缀
  r.push(rule('(?:^|\\n)\\s*[A-Za-z_][A-Za-z0-9_.-]*(?=\\s*:)', PALETTE.variable));  // 键
  r.push(rule('&[A-Za-z0-9_-]+|\\*[A-Za-z0-9_-]+|![A-Za-z0-9_!-]+', PALETTE.attr));   // 锚点/引用/标签
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('true false null yes no on off ~ True False Null NULL YES NO'));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('\\$\\([^)\\n]*\\)|\\$\\{[^}\\n]*\\}', PALETTE.function));
  return r;
});

// ---- TOML ------------------------------------------------------------------
def('toml', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule("'''[\\s\\S]*?(?:'''|$)", PALETTE.string));
  r.push(rule('(?:^|\\n)\\s*\\[\\[?[^\\]\\n]+\\]\\]?', PALETTE.attr));   // [table] / [[array]]
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('(?:^|\\n)\\s*[A-Za-z_][A-Za-z0-9_.-]*(?=\\s*=)', PALETTE.variable));
  r.push(constRule('true false'));
  r.push(rule('\\b\\d{4}-\\d{2}-\\d{2}(?:[T ]\\d{2}:\\d{2}(?::\\d{2})?)?\\b', PALETTE.constant));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- INI / .properties / .env ---------------------------------------------
def('ini', () => {
  const r = [];
  r.push(rule('[;#][^\\n]*', PALETTE.comment));
  r.push(rule('(?:^|\\n)\\s*\\[[^\\]\\n]+\\]', PALETTE.attr));
  r.push(rule('(?:^|\\n)\\s*(?:export\\s+)?[A-Za-z_][A-Za-z0-9_.-]*(?=\\s*[=:])', PALETTE.variable));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('true false null'));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('\\$\\{[^}\\n]*\\}', PALETTE.function));
  return r;
});

// ---- SQL（不区分大小写）---------------------------------------------------
def('sql', () => {
  const r = [];
  r.push(rule('--[^\\n]*', PALETTE.comment));
  r.push(rule('/\\*[\\s\\S]*?(?:\\*/|$)', PALETTE.comment));
  r.push(rule(`'(?:[^'\\\\\\n]|'')*'`, PALETTE.string));
  r.push(rule(`"(?:[^"\\\\\\n]|"")*"`, PALETTE.string));
  r.push(rule('`[^`\\n]*`', PALETTE.variable));                     // mysql 标识符
  r.push(rule('\\b(?:SELECT|FROM|WHERE|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|ALTER|DROP|TABLE|VIEW|INDEX|JOIN|LEFT|RIGHT|INNER|OUTER|FULL|CROSS|ON|GROUP|BY|ORDER|HAVING|LIMIT|OFFSET|UNION|ALL|DISTINCT|AS|AND|OR|NOT|NULL|IS|IN|EXISTS|LIKE|BETWEEN|CASE|WHEN|THEN|ELSE|END|WITH|RECURSIVE|RETURNING|PRIMARY|KEY|FOREIGN|REFERENCES|CONSTRAINT|UNIQUE|DEFAULT|CHECK|AUTO_INCREMENT|BEGIN|COMMIT|ROLLBACK|TRANSACTION|IF|EXPLAIN|ANALYZE|VACUUM|TRUNCATE|GRANT|REVOKE|OVER|PARTITION|WINDOW|USING|NATURAL|ASC|DESC)\\b', PALETTE.keyword));
  r.push(rule('\\b(?:select|from|where|insert|into|values|update|set|delete|create|alter|drop|table|view|index|join|left|right|inner|outer|full|cross|on|group|by|order|having|limit|offset|union|all|distinct|as|and|or|not|null|is|in|exists|like|between|case|when|then|else|end|with|recursive|returning|primary|key|foreign|references|constraint|unique|default|check|begin|commit|rollback|transaction|if|explain|analyze|vacuum|truncate|grant|revoke|over|partition|window|using|natural|asc|desc)\\b', PALETTE.keyword));
  r.push(rule('\\b(?:INT|INTEGER|BIGINT|SMALLINT|TINYINT|DECIMAL|NUMERIC|FLOAT|DOUBLE|REAL|BOOLEAN|BOOL|CHAR|VARCHAR|TEXT|BLOB|DATE|TIME|TIMESTAMP|DATETIME|INTERVAL|JSON|JSONB|UUID|SERIAL|BIGSERIAL|BYTEA|ARRAY)\\b', PALETTE.constant, { })); // 类型：大写
  r.push(rule('\\b(?:int|integer|bigint|smallint|tinyint|decimal|numeric|float|double|real|boolean|bool|char|varchar|text|blob|date|time|timestamp|datetime|interval|json|jsonb|uuid|serial|bigserial|bytea|array)\\b', PALETTE.constant));
  r.push(builtinRule('COUNT SUM AVG MIN MAX COALESCE NULLIF CAST CONVERT NOW CURRENT_DATE CURRENT_TIMESTAMP LOWER UPPER LENGTH SUBSTRING TRIM ROUND ABS ROW_NUMBER RANK DENSE_RANK LAG LEAD string_agg jsonb_agg array_agg count sum avg min max coalesce now lower upper length substring trim round abs'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- HTML ------------------------------------------------------------------
def('html', () => {
  const r = [];
  r.push(rule('<!--[\\s\\S]*?(?:-->|$)', PALETTE.comment));
  r.push(rule('<!\\[CDATA\\[[\\s\\S]*?(?:\\]\\]>|$)', PALETTE.string));
  r.push(rule('<!DOCTYPE[^>\\n]*>|<!doctype[^>\\n]*>', PALETTE.attr));
  r.push(rule(`(?:^|\\n)#[^\\n]*`, PALETTE.comment));                  // 仅模板场景
  r.push(rule('<(?:/)?([A-Za-z][A-Za-z0-9:-]*)', PALETTE.keyword));     // 标签名
  r.push(rule('\\b[A-Za-z_:][-A-Za-z0-9_:.]*(?==)', PALETTE.variable)); // 属性名
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('&[A-Za-z][A-Za-z0-9]*;|&#\\d+;|&#x[0-9a-fA-F]+;', PALETTE.constant));
  r.push(rule('@[A-Za-z-]+|v-[A-Za-z-]+|:[A-Za-z-]+', PALETTE.attr));    // Vue/Angular 指令
  return r;
});

// ---- XML -------------------------------------------------------------------
def('xml', () => {
  const r = [];
  r.push(rule('<!--[\\s\\S]*?(?:-->|$)', PALETTE.comment));
  r.push(rule('<!\\[CDATA\\[[\\s\\S]*?(?:\\]\\]>|$)', PALETTE.string));
  r.push(rule('<\\?[\\s\\S]*?(?:\\?>|$)', PALETTE.attr));               // <?xml ... ?>
  r.push(rule('<!DOCTYPE[^>\\n]*>', PALETTE.attr));
  r.push(rule('<(?:/)?([A-Za-z_:][A-Za-z0-9:._-]*)', PALETTE.keyword));
  r.push(rule('\\b[A-Za-z_:][-A-Za-z0-9_:.]*(?==)', PALETTE.variable));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('&[A-Za-z][A-Za-z0-9]*;|&#\\d+;|&#x[0-9a-fA-F]+;', PALETTE.constant));
  return r;
});

// ---- CSS / SCSS / Less -----------------------------------------------------
function cssFamily(id, o) {
  const r = [];
  if (o.lineComment) r.push(rule('//[^\\n]*', PALETTE.comment));
  r.push(rule('/\\*[\\s\\S]*?(?:\\*/|$)', PALETTE.comment));
  r.push(rule('#[0-9a-fA-F]{3,8}\\b', PALETTE.constant));                         // 色值
  r.push(rule('@[A-Za-z-]+', PALETTE.attr));                                       // @media / @import
  r.push(rule(`"${RE.dq}"|'${RE.sq}'`, PALETTE.string));
  r.push(rule('-{0,2}[A-Za-z-]+\\s*(?=:(?!:))', PALETTE.variable));               // 属性名（含 --vars）
  r.push(rule('(?:^|\\n)\\s*[.#&][A-Za-z_][A-Za-z0-9_-]*|(?:^|\\n)\\s*[A-Za-z][A-Za-z0-9-]*(?=\\s*\\{)', PALETTE.keyword)); // 选择器
  r.push(rule('\\.\\d+|\\b\\d+(?:\\.\\d+)?(?:px|em|rem|vh|vw|vmin|vmax|%|s|ms|deg|fr|ch|ex|pt|cm|mm|in|dpi)?\\b', PALETTE.constant));
  if (o.vars) r.push(rule('\\$[A-Za-z_][A-Za-z0-9_-]*', PALETTE.function));
  r.push(rule('var\\(--[A-Za-z0-9_-]+\\)|calc\\([^)\\n]*\\)', PALETTE.function));
  return r;
}
def('css', () => cssFamily('css', {}));
def('scss', () => cssFamily('scss', { lineComment: true, vars: true }));
def('less', () => cssFamily('less', { lineComment: true, vars: true }));

// ---- Markdown --------------------------------------------------------------
def('markdown', () => {
  const r = [];
  r.push(rule('```[\\s\\S]*?(?:```|$)', PALETTE.string));                 // 围栏块整体
  r.push(rule('`[^`\\n]+`', PALETTE.string));
  r.push(rule('(?:^|\\n)\\s{0,3}#{1,6}[^\\n]*', PALETTE.keyword));        // 标题
  r.push(rule('\\*\\*[^*\\n]+\\*\\*|__[^_\\n]+__', PALETTE.attr));        // 加粗
  r.push(rule('(?:^|\\n)\\s{0,3}>[^\\n]*', PALETTE.comment));             // 引用
  r.push(rule('(?:^|\\n)\\s{0,3}(?:[-*+]|\\d+\\.)\\s', PALETTE.punct));   // 列表
  r.push(rule('!?\\[[^\\]\\n]*\\]\\([^)\\n]*\\)', PALETTE.function));     // 链接/图片
  r.push(rule('(?:^|\\n)\\s{0,3}(?:---+|\\*\\*\\*+|___+)\\s*(?=\\n|$)', PALETTE.punct)); // 分割线
  return r;
});

// ---- Diff / Patch ----------------------------------------------------------
def('diff', () => {
  const r = [];
  r.push(rule('(?:^|\\n)(?:diff --git|index |new file|deleted file|similarity index|rename )[^\\n]*', PALETTE.comment));
  r.push(rule('(?:^|\\n)@@[^\\n]*', PALETTE.attr));
  r.push(rule('(?:^|\\n)\\+[^\\n]*', PALETTE.string));
  r.push(rule('(?:^|\\n)-[^\\n]*', PALETTE.keyword));
  return r;
});

// ---- Go --------------------------------------------------------------------
def('go', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('`[^`]*`', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('true false nil iota'));
  r.push(typeRule('bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any'));
  r.push(kw('break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var'));
  r.push(builtinRule('append cap close complex copy delete imag len make new panic print println real recover'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('(?<=\\.)[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  return r;
});

// ---- Rust ------------------------------------------------------------------
def('rust', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('#!?\\[[^\\]\\n]*\\]', PALETTE.attr));                      // #[derive] / #![allow]
  r.push(rule('r#*"[^"]*"#*', PALETTE.string));                           // raw string
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule("b?'(?:[^'\\\\\\n]|\\\\.)'", PALETTE.string));
  r.push(constRule('true false None Some Ok Err self Self'));
  r.push(typeRule('bool char str u8 u16 u32 u64 u128 usize i8 i16 i32 i64 i128 isize f32 f64 String Vec Option Result Box Rc Arc HashMap HashSet BTreeMap'));
  r.push(kw('as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move mut pub ref return static struct super trait type unsafe use where while'));
  r.push(builtinRule('println eprintln print format vec panic assert assert_eq assert_ne todo unimplemented unreachable dbg write writeln include_str include_bytes env concat stringify'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*[!(]|::)', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- Java / Kotlin / C#（JVM 家族，各自再补差异）---------------------------
def('java', () => {
  const r = [];
  r.push(rule(RE.blockComment('/**', '*/'), PALETTE.comment));
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule("'(?:[^'\\\\\\n]|\\\\.)'", PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));
  r.push(constRule('true false null this super'));
  r.push(typeRule('boolean byte char short int long float double void String Object Integer Long Double Float Boolean Character Byte Short List Map Set ArrayList HashMap HashSet Optional Stream'));
  r.push(kw('abstract assert break case catch class continue default do else enum extends final finally for goto if implements import instanceof interface native new package private protected public record return sealed static strictfp switch synchronized this throw throws transient try var volatile while yield permits non-sealed'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('kotlin', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule("'(?:[^'\\\\\\n]|\\\\.)'", PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));
  r.push(constRule('true false null this super it'));
  r.push(typeRule('Boolean Byte Char Short Int Long Float Double String Any Unit Nothing List Map Set MutableList MutableMap Array Pair Triple Sequence Flow'));
  r.push(kw('abstract actual annotation as break by catch class companion const constructor continue crossinline data delegate do dynamic else enum expect external field file final finally for fun get if import in infix init inline inner interface internal is lateinit noinline object open operator out override package private protected public reified return sealed set super suspend tailrec throw try typealias val var vararg when where while'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('csharp', () => cFamily('csharp', {
  preprocessor: true, attributes: true, call: true, member: true,
  consts: 'true false null this base',
  types: 'bool byte sbyte char decimal double float int uint long ulong short ushort object string void var dynamic',
  kws: 'abstract as async await base break case catch checked class const continue default delegate do else enum event explicit extern finally fixed for foreach get goto if implicit in init interface internal is lock namespace new operator out override params private protected public readonly record ref return sealed set sizeof stackalloc static struct switch throw try typeof unchecked unsafe using virtual volatile when where while yield nameof partial required',
  builtins: 'Console Math String Convert Task List Dictionary IEnumerable IList IQueryable Exception Guid DateTime TimeSpan Environment File Directory Path'
}));

// ---- C / C++ ---------------------------------------------------------------
def('c', () => cFamily('c', {
  preprocessor: true, call: true, member: true,
  consts: 'NULL true false',
  types: 'void char short int long float double signed unsigned size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t bool _Bool FILE',
  kws: 'auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while _Static_assert _Alignas _Alignof _Atomic _Generic _Noreturn _Thread_local',
  builtins: 'printf fprintf sprintf snprintf scanf fscanf sscanf malloc calloc realloc free memcpy memmove memset memcmp strlen strcpy strncpy strcat strncat strcmp strncmp strchr strrchr strstr strdup fopen fclose fread fwrite fgets fgetc fputc fflush fseek ftell rewind perror exit abort assert qsort bsearch toupper tolower isalpha isdigit isspace'
}));

def('cpp', () => cFamily('cpp', {
  preprocessor: true, attributes: true, call: true, member: true,
  consts: 'nullptr true false NULL',
  types: 'void bool char char8_t char16_t char32_t wchar_t short int long float double signed unsigned size_t ssize_t ptrdiff_t auto std string wstring vector map unordered_map set unordered_set array pair tuple optional variant unique_ptr shared_ptr weak_ptr function thread mutex istream ostream stringstream ifstream ofstream',
  kws: 'alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept bitand bitor break case catch class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern false for friend goto if inline mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public reflexpr register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union using virtual void volatile while xor xor_eq override final',
  builtins: 'std cout cin cerr endl printf malloc free memcpy memset strlen push_back emplace_back size empty begin end find insert erase clear make_unique make_shared move forward'
}));

// ---- PHP -------------------------------------------------------------------
def('php', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('\\$[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  r.push(rule('#\\[[^\\]\\n]*\\]', PALETTE.attr));
  r.push(constRule('true false null TRUE FALSE NULL __CLASS__ __FUNCTION__ __METHOD__ __LINE__ __FILE__'));
  r.push(typeRule('int float string bool array object callable iterable mixed void null self static parent'));
  r.push(kw('abstract and array as break callable case catch class clone const continue declare default do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile enum eval exit extends final finally fn for foreach function global goto if implements include include_once instanceof insteadof interface isset list match namespace new or print private protected public readonly require require_once return static switch throw trait try unset use var while xor yield'));
  r.push(builtinRule('strlen count explode implode array_map array_filter array_merge in_array sprintf printf print_r var_dump json_encode json_decode preg_match preg_replace str_replace substr trim strtolower strtoupper file_get_contents file_put_contents'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- Ruby ------------------------------------------------------------------
def('ruby', () => {
  const r = [];
  r.push(rule('=begin[\\s\\S]*?(?:=end|$)', PALETTE.comment));
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule(':[A-Za-z_][A-Za-z0-9_]*[?!]?', PALETTE.constant));      // 符号
  r.push(rule('@[A-Za-z_][A-Za-z0-9_]*|@@[A-Za-z_][A-Za-z0-9_]*|\\$[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  r.push(constRule('true false nil self __FILE__ __LINE__ __method__'));
  r.push(kw('alias and begin break case class def defined? do else elsif end ensure for if in module next not or redo rescue retry return super then undef unless until when while yield require require_relative attr_accessor attr_reader attr_writer include extend prepend'));
  r.push(builtinRule('puts print p gets raise lambda proc loop each map select reject reduce inject sort sort_by push pop shift unshift length size join split strip to_s to_i to_sym to_a new free'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*[?!]?(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- Lua -------------------------------------------------------------------
def('lua', () => {
  const r = [];
  r.push(rule('--\\[(=*)\\[[\\s\\S]*?(?:\\]\\1\\]|$)', PALETTE.comment));
  r.push(rule('--[^\\n]*', PALETTE.comment));
  r.push(rule('\\[(=*)\\[[\\s\\S]*?(?:\\]\\1\\]|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('true false nil'));
  r.push(kw('and break do else elseif end for function goto if in local not or repeat return then until while'));
  r.push(builtinRule('print pairs ipairs next type tostring tonumber error assert pcall xpcall require setmetatable getmetatable rawget rawset table string math io os coroutine'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- R ---------------------------------------------------------------------
def('r', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('TRUE FALSE NULL NA NaN Inf T F'));
  r.push(kw('if else repeat while function for in next break return library require source'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_.]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('(?<=\\$)[A-Za-z_][A-Za-z0-9_.]*', PALETTE.variable));
  return r;
});

// ---- Dart / Scala / Swift --------------------------------------------------
def('dart', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));
  r.push(constRule('true false null'));
  r.push(typeRule('int double num String bool List Map Set dynamic void Future Stream Object'));
  r.push(kw('abstract as assert async await break case catch class const continue covariant default deferred do else enum export extends extension external factory final finally for Function get hide if implements import in interface is late library mixin new on operator part required rethrow return sealed set show static super switch sync this throw try typedef var void while with yield'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('scala', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule("'(?:[^'\\\\\\n]|\\\\.)'", PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_.]*', PALETTE.attr));
  r.push(constRule('true false null None Some Nil'));
  r.push(typeRule('Int Long Double Float Boolean Char String Unit Any Nothing List Seq Map Set Option Either Future Array'));
  r.push(kw('abstract case catch class def do else enum export extends final finally for forSome given if implicit import lazy match new object opaque open override package private protected return sealed super then throw trait try type val var while with yield using derives end'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*[\\[(])', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('swift', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule('@[A-Za-z_][A-Za-z0-9_]*', PALETTE.attr));
  r.push(constRule('true false nil self Self super'));
  r.push(typeRule('Int Double Float Bool String Character Array Dictionary Set Optional Any Void Result Date URL'));
  r.push(kw('associatedtype class deinit enum extension fileprivate func import init inout internal let open operator private protocol public rethrows static struct subscript typealias var break case continue default defer do else fallthrough for guard if in repeat return switch where while as catch is throw throws try async await actor some any'));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- 配置 / 构建 ----------------------------------------------------------
def('dockerfile', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('(?:^|\\n)\\s*(?:FROM|RUN|CMD|LABEL|MAINTAINER|EXPOSE|ENV|ADD|COPY|ENTRYPOINT|VOLUME|USER|WORKDIR|ARG|ONBUILD|STOPSIGNAL|HEALTHCHECK|SHELL|AS)\\b', PALETTE.keyword));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(rule('\\$\\{[^}\\n]*\\}|\\$[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  r.push(rule('--[a-z-]+', PALETTE.attr));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('make', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('(?:^|\\n)[A-Za-z_][A-Za-z0-9_.]*\\s*(?=[:+?]?=)', PALETTE.variable));
  r.push(rule('(?:^|\\n)[A-Za-z0-9_./$(){}-]+(?=\\s*:(?!=))', PALETTE.function));   // 目标
  r.push(rule('\\$[@<^?*$#%]|\\$\\([^)\\n]*\\)|\\$\\{[^}\\n]*\\}', PALETTE.function));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(kw('ifeq ifneq ifdef ifndef else endif include define endef export unexport override'));
  return r;
});

def('cmake', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('\\$\\{[^}\\n]*\\}|\\$<[^>\\n]*>|\\$ENV\\{[^}\\n]*\\}', PALETTE.variable));
  r.push(rule('\\b(?:project|cmake_minimum_required|add_executable|add_library|add_subdirectory|target_link_libraries|target_include_directories|target_compile_definitions|target_compile_options|set|if|elseif|else|endif|foreach|endforeach|function|endfunction|macro|endmacro|include|find_package|install|option|message|list|string|file|configure_file|aux_source_directory|get_filename_component)\\b', PALETTE.keyword));
  r.push(rule('\\b(?:PUBLIC|PRIVATE|INTERFACE|REQUIRED|COMPONENTS|STREQUAL|AND|OR|NOT|TRUE|FALSE|ON|OFF)\\b', PALETTE.constant));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- 协议 / 数据 ----------------------------------------------------------
def('proto', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(kw('syntax package import option message enum service rpc returns stream repeated optional required reserved oneof map extend extensions group weak public'));
  r.push(typeRule('double float int32 int64 uint32 uint64 sint32 sint64 fixed32 fixed64 sfixed32 sfixed64 bool string bytes'));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('graphql', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule('"""[\\s\\S]*?(?:"""|$)', PALETTE.string));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(kw('query mutation subscription fragment on type interface union enum input scalar schema extend directive implements'));
  r.push(typeRule('Int Float String Boolean ID'));
  r.push(builtinRule('include skip deprecated specifiedBy'));
  r.push(rule('\\$[A-Za-z_][A-Za-z0-9_]*', PALETTE.variable));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('hcl', () => {
  const r = [];
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule('\\b(?:resource|data|variable|output|provider|module|terraform|locals|backend|required_providers)\\b', PALETTE.keyword));
  r.push(rule('(?:^|\\n)\\s*[A-Za-z_][A-Za-z0-9_-]*(?=\\s*=)', PALETTE.variable));
  r.push(constRule('true false null'));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

def('http', () => {
  const r = [];
  r.push(rule('(?:^|\\n)(?:GET|POST|PUT|PATCH|DELETE|HEAD|OPTIONS)\\s+\\S+', PALETTE.keyword));
  r.push(rule('(?:^|\\n)HTTP/\\d(?:\\.\\d)?\\s+\\d{3}[^\\n]*', PALETTE.constant));
  r.push(rule('(?:^|\\n)[A-Za-z-]+(?=:)', PALETTE.variable));
  r.push(rule('(?:^|\\n)#[^\\n]*', PALETTE.comment));
  r.push(rule('\\{\\{[^}\\n]*\\}\\}', PALETTE.function));
  return r;
});

def('bat', () => {
  const r = [];
  r.push(rule('(?:^|\\n)\\s*(?:rem\\b|REM\\b|::)[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule('%[A-Za-z_][A-Za-z0-9_]*%|![A-Za-z_][A-Za-z0-9_]*!', PALETTE.variable));
  r.push(rule('(?:^|\\n)\\s*@?[A-Za-z]+\\b', PALETTE.keyword));
  r.push(rule('\\b(?:echo|set|if|else|for|in|do|call|goto|exit|copy|move|del|md|rd|cd|pushd|popd|dir|type|find|findstr|start|pause|rem|setlocal|endlocal|errorlevel|exist|defined|not)\\b', PALETTE.keyword));
  r.push(rule(RE.number, PALETTE.constant));
  return r;
});

// ---- 通用兜底（无语言 / 不认识的语言）-------------------------------------
def('default', () => {
  const r = [];
  r.push(rule(RE.blockComment('/*', '*/'), PALETTE.comment));
  r.push(rule(RE.lineComment('//'), PALETTE.comment));
  r.push(rule('#[^\\n]*', PALETTE.comment));
  r.push(rule(`"${RE.dq}"`, PALETTE.string));
  r.push(rule(`'${RE.sq}'`, PALETTE.string));
  r.push(constRule('true false null undefined nil None True False NULL TRUE FALSE'));
  r.push(rule('\\b(?:if|else|for|while|do|switch|case|break|continue|return|function|func|def|class|struct|enum|import|from|export|const|let|var|new|delete|try|catch|finally|throw|throws|public|private|protected|static|final|abstract|interface|extends|implements|in|of|is|and|or|not|async|await|yield|lambda|pass|raise|with|as)\\b', PALETTE.keyword));
  r.push(rule(RE.number, PALETTE.constant));
  r.push(rule('\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()', PALETTE.function));
  return r;
});

// ── 别名表（对齐官方 LANG_ALIASES，fence 标签/文件扩展名/短 id 都能落到同一套规则）──
const ALIASES = {
  // JS 家族全部映射到 typescript（官方同样只用一份 TS 语法）
  typescript: 'typescript', ts: 'typescript', tsx: 'typescript', mts: 'typescript', cts: 'typescript',
  javascript: 'typescript', js: 'typescript', jsx: 'typescript', mjs: 'typescript', cjs: 'typescript', node: 'typescript',
  json: 'json', jsonc: 'json', json5: 'json', geojson: 'json',
  python: 'python', py: 'python', python3: 'python', py3: 'python',
  shellscript: 'shellscript', bash: 'shellscript', sh: 'shellscript', shell: 'shellscript',
  zsh: 'shellscript', ksh: 'shellscript', console: 'shellscript', shellsession: 'shellscript', terminal: 'shellscript',
  powershell: 'powershell', ps1: 'powershell', ps: 'powershell', pwsh: 'powershell', posh: 'powershell',
  yaml: 'yaml', yml: 'yaml',
  toml: 'toml',
  ini: 'ini', properties: 'ini', cfg: 'ini', conf: 'ini', editorconfig: 'ini', dotenv: 'ini', env: 'ini',
  sql: 'sql', mysql: 'sql', postgres: 'sql', postgresql: 'sql', sqlite: 'sql', plsql: 'sql', tsql: 'sql',
  html: 'html', htm: 'html', vue: 'html', svelte: 'html', astro: 'html', xhtml: 'html',
  xml: 'xml', svg: 'xml', xsl: 'xml', plist: 'xml', rss: 'xml',
  css: 'css', scss: 'scss', sass: 'scss', less: 'less',
  markdown: 'markdown', md: 'markdown', mdx: 'markdown', mkd: 'markdown',
  diff: 'diff', patch: 'diff',
  go: 'go', golang: 'go',
  rust: 'rust', rs: 'rust',
  java: 'java',
  c: 'c', h: 'c',
  cpp: 'cpp', 'c++': 'cpp', cc: 'cpp', cxx: 'cpp', hpp: 'cpp', hh: 'cpp', hxx: 'cpp', ipp: 'cpp',
  csharp: 'csharp', cs: 'csharp', 'c#': 'csharp', csx: 'csharp',
  kotlin: 'kotlin', kt: 'kotlin', kts: 'kotlin',
  swift: 'swift',
  php: 'php',
  ruby: 'ruby', rb: 'ruby', gemfile: 'ruby', rake: 'ruby',
  lua: 'lua',
  r: 'r', rscript: 'r',
  dart: 'dart',
  scala: 'scala',
  dockerfile: 'dockerfile', docker: 'dockerfile', containerfile: 'dockerfile',
  make: 'make', makefile: 'make', mk: 'make',
  cmake: 'cmake',
  proto: 'proto', protobuf: 'proto', proto3: 'proto',
  graphql: 'graphql', gql: 'graphql',
  hcl: 'hcl', tf: 'hcl', terraform: 'hcl', tfvars: 'hcl',
  http: 'http', rest: 'http',
  bat: 'bat', batch: 'bat', cmd: 'bat', dos: 'bat',
  // 纯文本：显式落到 default（= 只有通用规则），避免与"未知语言"混淆
  text: 'default', txt: 'default', plain: 'default', plaintext: 'default', log: 'default', output: 'default', csv: 'default', tsv: 'default'
};

// ── 组装 ────────────────────────────────────────────────────────────────────
const languages = {};
for (const id of Object.keys(LANGS)) {
  languages[id] = LANGS[id]();
}

const out = {
  _readme:
    '语法高亮规则。引擎见 source/src/common/util/CodeHighlighter.cpp：' +
    '对每个位置在所有规则里取"最早命中、同位置最长者胜"的一条，命中段套 <span style="color:...">，' +
    '其余原样转义。language 先经 aliases 归一（大小写不敏感、只取 fence 标签的第一个词），' +
    '再查 languages；查不到就用 default。',
  _source:
    '配色 = DSH 官方版 Shiki 调色板（dsh-client-ui-theme 的 shiki.css，light/dark 两套取中间值）；' +
    '语言/别名集对齐官方 @deepseek-ai/dsh-client-ui-primitives 的 LANG_ALIASES（官方用 Shiki 的 TextMate 语法，' +
    '本版用等价的手写正则规则，见 docs/UI-OFFICIAL-ALIGNMENT.zh-CN.md）。',
  _generated:
    '⚠️ 本文件由 misc/tools/gen-highlight-rules.js 生成，不要手改；改词表请改那个脚本再重跑。',
  aliases: ALIASES,
  languages
};

const target = path.join(__dirname, '..', '..', 'assets', 'resources', 'config', 'highlight-rules.json');
fs.writeFileSync(target, JSON.stringify(out, null, 2) + '\n', 'utf8');

const langCount = Object.keys(languages).length;
const ruleCount = Object.values(languages).reduce((n, r) => n + r.length, 0);
const bytes = fs.statSync(target).size;
console.log(`写出 ${target}`);
console.log(`  语言 ${langCount} 个、规则 ${ruleCount} 条、别名 ${Object.keys(ALIASES).length} 条、${(bytes / 1024).toFixed(1)} KB`);
