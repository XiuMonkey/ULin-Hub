// 从引擎自带包里抽出浏览器版 xterm.js / addon-fit / xterm.css，产出右栏终端面板用的 qrc 资产。
//
// 为什么是"抽"而不是装包：@xterm/xterm 在本仓库的 node_modules 里**没有独立包**，但引擎自带的
// 官方侧栏终端 bundle（@deepseek-ai/dsh-client-ui-sidebar-terminal/lib/client.terminal.js）已经把
// @xterm/xterm@6.0.0 与 @xterm/addon-fit@0.11.0 打了进去（rolldown 的 CommonJS region）。
// 从那里切出来 = 与引擎使用的版本天然一致，且**零新增三方依赖**。
//
// 来源与版本（复核时照这几条对账）：
//   · 源文件：assets/resources/server/node_modules/@deepseek-ai/dsh-client-ui-sidebar-terminal/lib/client.terminal.js
//   · region 注释里写死了版本：@xterm+xterm@6.0.0 / @xterm+addon-fit@0.11.0（许可证：MIT）
//   · 目标目录：assets/resources/Terminal/（qrc 前缀 ULinHub ⇒ qrc:/ULinHub/Terminal/…）
//
// 用法（在仓库根，或任意目录；参数都可省）：
//   node misc/tools/extract-xterm.mjs [--src <client.terminal.js>] [--out <目录>]
// 省参时：src = <repo>/assets/resources/server/node_modules/…/lib/client.terminal.js，
//         out = <repo>/assets/resources/Terminal
// 产物：xterm.browser.js（window.__xterm）/ addon-fit.browser.js（window.__fit）/ xterm.css
// 幂等：内容不变时逐字节一致；改完请把三个文件一起提交（它们不是构建期生成的，是入库资产）。
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '..', '..');

function arg(name) {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : null;
}

const SRC = arg('src') || join(repoRoot, 'assets', 'resources', 'server', 'node_modules',
  '@deepseek-ai', 'dsh-client-ui-sidebar-terminal', 'lib', 'client.terminal.js');
const OUTDIR = resolve(arg('out') || join(repoRoot, 'assets', 'resources', 'Terminal'));

if (!existsSync(SRC)) {
  console.error('找不到源 bundle：' + SRC + '\n（引擎版本换了路径就显式传 --src）');
  process.exit(2);
}

const text = readFileSync(SRC, 'utf8');

// 每个 region 的形状： var require_X = /* @__PURE__ */ __commonJSMin(((exports, module) => { BODY }));
//                    //#endregion
const OPEN = '__commonJSMin(((exports, module) => {';
const CLOSE = '\n\t\t}));\n\t\t//#endregion';

function nthRegion(n) {
  let idx = -1;
  for (let i = 0; i <= n; i++) idx = text.indexOf(OPEN, idx + 1);
  if (idx < 0) throw new Error('第 ' + n + ' 个 CommonJS region 没找到（引擎 bundle 结构变了？）');
  const e = text.indexOf(CLOSE, idx);
  if (e < 0) throw new Error('第 ' + n + ' 个 region 的结束标记没找到');
  return text.slice(idx + OPEN.length, e);
}

// 把 CJS region 包成浏览器全局：跑一遍 region 体（它内部自己 module.exports = …），
// 结果挂到 window 上给页面用。
const CJS_HEAD = 'module.exports = (function(){ var module = { exports: {} }; var exports = module.exports;\n';
const CJS_TAIL = '\nreturn module.exports; })();\n';

function browserBuild(regionIndex, globalName) {
  const body = nthRegion(regionIndex);
  return 'window.' + globalName + ' = (function(){ var module = { exports: {} }; var exports = module.exports;\n'
    + body + CJS_TAIL + '\n';
}

mkdirSync(OUTDIR, { recursive: true });
const sizes = [];

function emit(name, content) {
  const file = join(OUTDIR, name);
  writeFileSync(file, content, 'utf8');
  sizes.push([name, Buffer.byteLength(content, 'utf8')]);
}

emit('xterm.browser.js', browserBuild(0, '__xterm'));
emit('addon-fit.browser.js', browserBuild(1, '__fit'));

// xterm.css 在后面的 region 里是一个普通字符串常量，region 注释带 @dsh-global-css 标记
const cssTag = text.indexOf('dsh-global-css:');
if (cssTag < 0) throw new Error('没找到 @dsh-global-css region（引擎 bundle 结构变了？）');
const cssStart = text.indexOf('const css$1 = "', cssTag);
const cssEnd = text.indexOf('";\n', cssStart);
if (cssStart < 0 || cssEnd < 0) throw new Error('xterm.css 字符串常量没找到');
emit('xterm.css', text.slice(cssStart + 'const css$1 = "'.length, cssEnd).replace(/\\"/g, '"'));

console.log('extracted -> ' + OUTDIR);
for (const [name, bytes] of sizes) console.log('  ' + name.padEnd(24) + bytes + ' bytes');
