# 向 DSH 官方版对齐：设计系统对照与落地记录

> 目标：把 **ULin Hub 客户端（Qt/QML）** 的观感往 **DSH 官方版**（Electron + React 的那套 Web 客户端）靠。
> 本文所有"官方值"都不是感觉，而是从官方安装包的 `app.asar` 里把 CSS 全量还原后逐条读出来的。

---

## 0. 结论先说

差距不在"某一处没调好"，而在**三处体系性方言不同**：

| # | 根因 | 表现 | 本次处理 |
| --- | --- | --- | --- |
| 1 | **色板方言不同**：我们这套是 Tailwind 的 slate/navy（`#111827`/`#1F2937`/`#4C8BF5`），官方是自建 **neutral-bluish** 近黑体系（`#151517`/`#232324`/`#7AAAFF`） | 暗色整屏发蓝、发灰、发"糊"；文字比官方暗一档；用户气泡是饱和海军蓝 | 两套 theme JSON 全量重写为官方 alias 值 |
| 2 | **描边厚一倍**：官方所有"浮起面"的边不是 `border`，是 `--dsw-elevation-stroke: 0 0 0 .5px` 的**半像素环**；我们统一是 1 逻辑px（200% 缩放下 = 2 物理px） | 卡片/输入框"边缘太硬、太实"，这是上一轮你也提过的同一件事 | `Card` 与胶囊改 0.5px |
| 3 | **语法高亮是另一个年代**：`highlight-rules.json` 用的是 VS Code Dark+（纯蓝 `#0000ff` + 橙 `#ce9178` + 绿注释 `#6a9955`） | 代码块一眼"旧 IDE" | 110 条规则换成官方 Shiki 粉彩调色板，去掉 18 处 bold |

另有一批**结构级**差距（内容列居中、平面布局、图标体系、磨砂浮层）见 §4，属于要拍板再动的部分。

---

## 1. 证据来源：官方 UI 到底长什么样

官方客户端没有源码，但它的**前端产物和设计 token 全在**：

```
D:\Program Files\DSH Official\resources\app.asar          (121 MB，asar 归档)
  └─ dsh/node_modules/@deepseek-ai/
       ├─ dsh-client-ui-theme/lib/client.js                  ← 设计 token 全在这里（101 KB）
       │    · base.css           字体族 / 圆角档 / 缓动 / 过渡时长
       │    · design-platform.css  static 调色板 55 色 + alias 语义层 120 键（light + dark）
       │    · gradient-shadow-text.css  lv1/lv2/lv3 阴影 + elevation 半像素环 + 字号阶梯 40 条
       │    · scrollbar.css / focus.css / corner-shape.css
       ├─ dsh-client-ui-primitives/lib/**/*.module.css       ← 组件级样式（38 个 CSS module）
       └─ dsh-client-ui-{chat,conversation,sidebar,layout,...}/lib/client.js  ← 布局与容器
```

还原工具都在 `build/temp/official-ui/`（`build/` 已在 `.gitignore` 里，所以只作本地复现用）：

| 脚本 | 作用 |
| --- | --- |
| `asar-ls.js` | 列出 asar 目录树（`dataOffset = 8 + headerPickleSize`） |
| `asar-get.js` | 按内部路径抽单个文件 |
| `asar-extract-many.js` | 按子串批量抽（本次抽了 `@deepseek-ai/dsh-client-ui*` 共 415 个文件 / 12.5 MB） |
| `theme-css.js` | 把 theme 包里内嵌的 `css$N` 字符串还原成 9 个 .css |
| `css-pretty.js` | 单行压缩 CSS 展开成一行一声明，便于 diff |
| `extract-component-css.js` | 把 26 个 client-ui 包里 `\0dsh-css:` 区域的 CSS 还原成 26 个 .css |
| `shot.ps1` / `click-shot.ps1` | `PrintWindow(PW_RENDERFULLCONTENT)` 抓本版窗口（不抢前台）；后者可按窗口内坐标点一下再抓 |

复现：

```powershell
$node = 'assets\resources\server\node.exe'          # 仓库自带 node，不用另装
& $node build\temp\official-ui\asar-extract-many.js `
    'D:\Program Files\DSH Official\resources\app.asar' `
    'node_modules/@deepseek-ai/dsh-client-ui' `
    'build\temp\official-ui\asar\dsh\client-ui'
```

---

## 2. 官方 token 全表（本次对齐的基准）

### 2.1 static 调色板（节选，`--dsw-static-*`）

| 族 | 关键色 |
| --- | --- |
| neutral-bluish（**主体系**） | 00 `#fff` · 50 `#f9fafb` · 60 `#f5f6f7` · 75 `#f1f3f5` · 100 `#ebeef2` · 150 `#e9ecf2` · 200 `#e1e5ee` · 300 `#cfd3d6` · 400 `#adb2b8` · 500 `#979da6` · 600 `#81858c` · 700 `#61666b` · 750 `#43454a` · 800 `#353638` · 850 `#2c2c2e` · 875 `#232324` · 900 `#1b1b1c` · 950 `#151517` · 1000 `#0f1115` |
| deepseek（品牌蓝） | 50 `#edf3fe` · 100 `#e4edfd` · 400 `#7aaaff` · 450 `#5686fe` · 500 `#4176e6` |
| 状态色 | red-600 `#ec1313` / red-400 `#f25a5a` · green-500 `#22c55e` / green-400 `#4ed17e` · amber-500 `#f59e0b` |

### 2.2 语义层 alias（决定观感的那一层）

| alias | light | dark |
| --- | --- | --- |
| `bg-base`（页面底） | `#ffffff` | `#151517` |
| `bg-layer-1/2/3`（卡片/浮层） | `#ffffff` | `#232324` / `#2c2c2e` / `#353638` |
| `bg-module-platform` | `#f5f6f7` | `#353638` |
| `specific-sidebar-fill` | `#f9fafb` | `#1b1b1c` |
| `specific-bubble`（**用户气泡**） | `#edf3fe` | `#2c2c2e` |
| `specific-input-major`（输入卡） | `#ffffff` | `#2c2c2e` |
| `label-primary` | `#0f1115` | `#f9fafb` |
| `label-secondary` | `#61666b` | `#cfd3d6` |
| `label-tertiary` | `#81858c` | `#adb2b8` |
| `label-caption` | `#a2a4a6` | `#81858c` |
| `border-l1 / l2 / l3 / l4` | `#0000000a` / `1a` / `1f` / `29`（4/10/12/16%） | `#ffffff0f` / `1f` / `29` / `fff3`（6/12/16/20%） |
| `interactive-bg-hover` / `active` | `#2631480f` / `1a` | `#ffffff14` / `24` |
| `link` / `state-business-primary` | `#4176e6` | `#7aaaff` |
| `button-primary-fill`（**主按钮**） | `#0f1115`（近黑面 + 白字） | `#f9fafb`（近白面 + 近黑字） |
| `button-elevated-fill`（次要面） | `#ffffff` | `#43454a` |
| `scrollbar-bg-l1` / `hover` | `#e5e5e5` / `#d4d4d4` | `#3c3c3d` / `#545557` |
| `tooltip-bg` / `toast-bg` | `#2c2c2e` / `#353638` | `#43454a` / `#43454a` |
| `markdown-code-block` / `banner` | `#f9fafb` / `#f9fafb` | `#1b1b1c` / `#232324` |
| `markdown-inline-code` | `#fafafa` | `#292929` |
| `menu-surface-fill` | `#f8f9fa94` | `#43454a73`（+ `backdrop-filter: blur(40px) saturate(150%)`） |

### 2.3 尺度 token

| 项 | 官方值 |
| --- | --- |
| 圆角 | `xs 4 / sm 8 / md 12 / lg 16 / xl 20 / panel 28`（**只有这 6 档**） |
| 字号 | `11/14 · 12/18 · 13/20 · 14/22 · 16/24 · 20/28 · 24/32`；markdown base **14/24**；code block **11/19**；inline code `0.875em` + radius 8 + 0.5px 边 |
| 阴影 | `lv1 0 2px 4px #0000000d` · `lv2 0 4px 12px #00000005, 0 2px 8px #0000000a` · `lv3 0 0 1px #0003, 0 0 4px #00000005, 0 12px 32px #00000014` |
| 高度环 | `elevation-stroke = 0 0 0 .5px`（light l4 / dark l3）· `panel = stroke + 0 3px 8px 8 + 0 0 16px 5` · `soft = stroke + 0 4px 16px 8 + 0 0 24px 8` · `prominent = stroke + 0 3px 8px a + 0 0 20px d` |
| 动效 | `100ms / 200ms / 300ms`，曲线 `cubic-bezier(.4, 0, .2, 1)` |
| 滚动条 | **5px**，thumb radius 999，轨道透明 |
| 按钮 | `md 36px / 14px / radius 12`，`sm 28px / 12px / radius 8`；ghost 只有 hover 底色；outline 用 **0.5px l3** |
| 输入框 | 高 32，**0.5px l4**，radius 12 |
| 分段控件 | 轨道 `interactive-bg-hover` + padding 4 + radius 12；指示器 `bg-layer-1` + `elevation-soft`；tab 28 高 / 13px / 500 |
| 聊天列 | 内容宽 `clamp(680px, 列宽 × .64, 920px)` 居中；输入卡 `内容宽 + 32`（≤952）居中；输入卡上方 36px 渐变遮罩 |
| 输入卡 | radius **28**（`radius-panel`）+ `specific-input-major` + `elevation-soft`，文本上限 336px |
| 代码块 | radius **16** + banner（padding `9px 14px`、11/18）+ 正文 padding 16 + 11/19 |
| 圆角形状 | `corner-shape: superellipse(1.5)`（苹果式 squircle） |

---

## 3. 逐项对照：我们 vs 官方

### 3.1 色板（改前 → 官方值）

**暗色**

| token | 改前（Tailwind） | 官方 | 判定 |
| --- | --- | --- | --- |
| `windowBg` | `#111827` slate-900 | `#151517` | ✗ 整屏发蓝的根源 |
| `panelBg` | `#1F2937` slate-800 | `#232324` | ✗ |
| `composerBg` | `#1F2937` | `#2C2C2E` | ✗ |
| `hoverBg` / `activeBg` | `#374151` / `#414F6B` | `#2C2C2E` / `#353638` | ✗ 前者太亮、后者发蓝 |
| `textSecondary` | `#9CA3AF` | `#CFD3D6` | ✗ 暗一档半 |
| `textTertiary` | `#8A9199` | `#ADB2B8` | ✗ |
| `userBubbleBg` | `#0C4A6E` sky-900 | `#2C2C2E` | ✗ 饱和海军蓝 → 中性面 |
| `scrollbar` | `#4B5563` | `#3C3C3D` | ✗ |
| `accent` | `#4C8BF5` | `#7AAAFF` | ✗ 过饱和 |
| `primaryButtonBg` | `#6E7681` | `#F9FAFB` | ✗ 最"土"的一处（见下图） |
| 滚动条粗细 | 8px | 5px | ✗ |

**亮色**

| token | 改前 | 官方 |
| --- | --- | --- |
| `windowBg` | `#FBFCFE`（带蓝的白） | `#F5F6F7` |
| `textPrimary` | `#1F2328` | `#0F1115` |
| `textSecondary` | `#6B7280` | `#61666B` |
| `userBubbleBg` | `#E0F2FE` sky-100 | `#EDF3FE` deepseek-50 |
| `primaryButtonBg` | `#E8EBEF` 灰面 | `#0F1115` 近黑面 |
| `scrollbar` | `#D1D5DB` | `#E5E5E5` |

### 3.2 尺度

| token | 改前 | 官方 | 判定 |
| --- | --- | --- | --- |
| `radius.card` / `control` / `input` | 10 / 8 / 8 | 12 / 12 / 12 | ✗ 并且 10、9、6、5 这四个值官方**根本不存在** |
| `radius.composer` | 16 | **28** | ✗ 输入卡的"高级感"一半来自这个 28 |
| `radius.menu` | 12 | 16（lg）/ 12（compact） | △ |
| `shadow.cardBlur/dy` | 8 / 2 | elevation-panel = 3px 8px + 0 16px | ✓ 接近 |
| `shadow.inputBlur/dy` | 8 / 2 | elevation-soft = 4px 16px + 0 24px | ✗ 官方输入卡阴影明显更"软更开" |
| `motion` | 120 / 180 / 260 | 100 / 200 / 300 | △ |
| 正文 | 13px | 14/24 | ✗ 13 是官方的 `xs-13`（次要信息），正文不该停在这一档 |
| 代码块 | 12px + 1px 描边 + 头行在外 | 11/19 + radius 16 + banner 在内 + **无描边** | ✗ |
| 语法色 | VS Code Dark+ | Shiki 粉彩（keyword `#d6336c`/`#faa2c1`、string `#2f9e44`/`#69db7c`、function `#6741d9`/`#b197fc`） | ✗ |
| 卡片描边 | 1 逻辑px（2 物理px） | 0.5px 环（1 物理px） | ✗ |

---

## 4. 本次已落地

构建验证：`MSBuild source\ULin Hub.vcxproj /p:Configuration=Release /p:Platform=x64` ✅ 通过。

| 文件 | 改动 | 依据 |
| --- | --- | --- |
| `assets/resources/styles/theme-dark.json` | **全量重写**为官方 dark alias；新增 `sessionBarBg/codeBlockBg/codeBlockBannerBg/inlineCodeBg/trackBg/indicatorBg/ringBg/dangerText/linkText/toolbarBg` | §2.2 |
| `assets/resources/styles/theme-light.json` | 同上（light alias） | §2.2 |
| `source/qml/metrics.json` | 圆角收敛到 `4/8/12/16/20/28`（card 10→12、control 8→12、input 8→12、small 6→8、menu 12→16、composer 16→28、inner 9→11）；滚动条 8→**5**；输入卡阴影 → elevation-soft（16/4）；动效 100/200/300 | §2.3 |
| `source/qml/widgets/Card.qml` | 描边 1 → **0.5** | `--dsw-elevation-stroke` |
| `source/qml/widgets/ActionButton.qml` | 新增 `elevated`（官方 secondary 面，灰面/白面 + 4% 环）/ `ghost` / `ghostDanger` 三种 variant，radius 12；danger 变体改用专属字色 `dangerButtonText`（不再借 `textOnAccent`，那个 token 现在跟着主按钮走） | `Button.module.css` |
| `source/qml/basic/session/SessionBar.qml` | 「清空会话」保持**实心 danger 胶囊**（原色 `dangerButtonBg`）；「新建工作区」→ `elevated` | 见 §4.1 |
| `source/qml/basic/shell/Shell.qml` | 侧栏卡片底色 → `sessionBarBg`（dark `#1B1B1C`；light 保持 `#FFFFFF`） | `specific-sidebar-fill` |
| `source/qml/widgets/SegmentedTabs.qml`（新）+ `qml.qrc` | 官方分段控件：轨道 + 滑动指示器（160ms），替换两个灰胶囊 | `SegmentedControl.module.css` |
| `source/qml/basic/topbar/TopBar.qml` | 「对话 / 轨迹」改用分段控件 | 同上 |
| `source/qml/skin/chat/CodeBlock.qml` | 官方块形态：radius 16、banner 34px（11px 等宽字 + 复制键 + 0.5 分隔线）、正文 padding 16、代码 **11px**、去掉 1px 硬描边、底色 `codeBlockBg` | `markdown/CodeBlock.module.css` |
| `source/qml/skin/chat/AgentBubble.qml` / `UserBubble.qml` | 正文字号：试过 14px（官方 markdown base），按用户口径**已还原为 13px** | 见 §4.1 |
| `assets/resources/config/highlight-rules.json` | 语法高亮：从手写 18 语言/110 条规则，改为**生成器产物** 39 语言 / 377 规则 / 134 别名（配色全部走官方 Shiki 9 色）；C++ 侧新增 fence 标签别名归一（`py`/`js`/`c++` 等一律落位）。详见 [SYNTAX-HIGHLIGHTING.zh-CN.md](SYNTAX-HIGHLIGHTING.zh-CN.md) | `shiki.css` + 官方 `LANG_ALIASES` |
| `misc/tools/gen-highlight-rules.js`（新） | 高亮规则生成器：改词表 → 重跑 → 产物 JSON（避免手改 54 KB 规则表） | 同上 |
| `source/qml/basic/chat/MessageListView.qml` | 「加载更多」胶囊 0.5px 环 | 同上 |

### 4.1 按用户要求还原的三处（别再改回去）

| 项 | 最终值 | 说明 |
| --- | --- | --- |
| light `windowBg` | **`#F7F8F9`** = 3/4 × 我的 `#F5F6F7` + 1/4 × 原 `#FBFCFE`（R 245·.75+251·.25=247，G 246·.75+252·.25=248，B 247·.75+254·.25=249） | 用户要求"我的颜色占四分之三、原色占四分之一"。纯官方值是 `bg-module-platform #F5F6F7`，纯原值是 `#FBFCFE`，取加权 |
| light `sessionBarBg` | `#FFFFFF` | 随同还原（官方 `specific-sidebar-fill` 是 `#F9FAFB`）。连带影响：`buttonBg` 必须留在浅灰 `#F1F3F5`，否则「新建工作区」的白面与白侧栏糊在一起、按钮会"消失" |
| dark `sessionBarBg` | **`#232324`**（与顶栏/会话卡片同色 = `cardBg`） | 用户要求"暗色下侧栏颜色和顶部栏对齐"。先前试过官方的 `#1B1B1C`（比内容面深一档）、也试过还原成 `#1F2937`，最终取**与顶栏一致**：暗色下侧栏、顶栏、会话卡片三块同色 `#232324`，只有输入卡 `#2C2C2E` 和窗口底 `#151517` 不同 —— 层次靠这两档差 + 投影，不靠侧栏单独一色 |
| 气泡正文字号 | `Theme.font.body`（13px） | 官方 markdown base 是 14/24；用户要"小一些"，还原为 13。Agent/User 两侧必须同档（含 `UserBubble` 里量宽用的隐形 Text） |
| 「清空会话」按钮 | `variant: "danger"` + 原色 `dangerButtonBg`（light `#D96A6E` / dark `#C96A6A`），悬停 `danger`，字色 `dangerButtonText #FFFFFF` | 试过 ghost 红字方案，用户要求"颜色还原"⇒ 回到实心胶囊与原红色。`danger`/`dangerBg`/`dangerText` 也随之还原；`dangerButtonText` 是为此新增的独立字色 token |

### 4.2 前后对比（同机、同 200% DPI、同会话）

| | 改前 | 改后 |
| --- | --- | --- |
| 暗色 | `build/temp/official-ui/shots/before-dark.png` | `build/temp/official-ui/shots/final-dark.png` |
| 亮色 | `build/temp/ui-texture/shots/after-01-shell.png`（上一轮） | `build/temp/official-ui/shots/final-light.png` |
| 设置页 | — | `build/temp/official-ui/shots/final-settings-light.png` |

对照能看出来的四件事：

1. 暗色从"蓝灰泥"变成官方的**中性近黑**，`#151517` 页面 + `#232324` 卡片 + `#1b1b1c` 侧栏，层次靠面色差而不是靠线；
2. 文字亮一档（`#CFD3D6`），长文可读性明显变好；
3. 侧栏两个大色块（红胶囊 + 灰胶囊）变成一枚安静的红字 + 一枚白/灰浮起面 —— 第一眼"土"的感觉主要来自这两块；
4. 代码块从"近白底 + 双硬线 + VS Code 蓝绿"变成"官方 16 圆角 + banner + 粉彩语法 + 11/19 等宽"。

---

## 5. 还没对齐的（按性价比排序，需要拍板）

### P1 · 内容列居中（官方最显眼的结构差异）

官方聊天列是**居中定宽**：`内容 = clamp(680px, 列宽 × 0.64, 920px)`，输入卡 `= 内容 + 32`（≤952）且同样居中，输入卡上方还有 36px 的 `linear-gradient(180deg, transparent 0, bg-base 36px)` 遮罩让内容从卡底下淡出。
我们现在是**整列铺满**（`Shell.qml` 的 `columnClearance` 内缩 16px）。窗口越宽，差距越明显。
改动面：`Shell.qml`（composer/列表的左右 margin 由居中公式算）+ `MessageListView`（左右 margin）+ `topBar` 对齐。≈ 40 行，风险中。

### P2 · 从"卡片布局"改"平面布局"

官方是 `侧栏 | 内容` 两栏整面（`bg-base` 一铺到底，只有输入卡浮起），我们是"两侧留 24px + 每个区域一张圆角卡片 + 投影"（旧 DSH Hub 的血统）。
这是**最大的一次观感跃迁**，也是最能一眼看出"像不像官方"的地方。但会动 Shell 的整体骨架，建议单独一轮做。

### P3 · 图标体系

官方 38 个组件全都用 SVG 图标集（`IconLightOutlineMedium` 之类，1.1em/1.5px stroke）；我们仍在用字形符号（`◫ ☰ ⧉ ＋ ▾`）。
字形在不同字体/DPI 下粗细不匀，这是"看着不精致"的隐性来源之一。建议引入一套 16/20px outline 图标（可直接按官方语义命名），`IconButton` 支持 `icon.source`。

### P4 · 浮层磨砂

官方菜单：`background: #f8f9fa94` + `backdrop-filter: blur(40px) saturate(150%)`；tooltip 圆角 8、padding `3px 7px`、13px。
QML 可用 `MultiEffect` + 半透明底近似（真正 backdrop-filter 不可得），或降级为更实的底色。

### P5 · Markdown 排版细节

官方：段落 `margin: 16px 0`；`h1 21/30 · h2 19/28 · h3 18/26 · h4 14/24`；列表 `padding-left: 18px`、`li + li` 6px；`hr` 0.5px；`blockquote` 2px 左边 + `padding-left: 14px`；表格 `th/td padding 10px 16px` + 0.5px 底线；inline code `0.875em` + radius 8 + 0.5px 边。
我们现在是 `TextEdit.MarkdownText` 的默认间距（段落几乎无间距、标题不缩放）。要真对齐需要把 markdown 预处理成带间距的富文本（`MarkdownPreprocess` 已经在，可以扩）。

### P6 · 0.5px 推广

官方 `Input`、`outline` 按钮、inline code、表格分隔线全是 **0.5px**（light l4 / l3）。我们目前只有 `Card` 与「加载更多」改了 0.5；`Composer`、`SearchField`、`FieldBox`、设置页的输入框还是 1px。

### P7 · 曲线与圆角形状

- 统一到 `cubic-bezier(.4, 0, .2, 1)`（我们多数用默认 `Easing.InOutQuad`）；QML 写法：`easing.bezierCurve: [0.4, 0, 0.2, 1]`。
- 官方 `corner-shape: superellipse(1.5)`（squircle）。QML 的 `Rectangle` 是正圆角；要 squircle 得用 `Shape`/自定义 mesh，收益小、成本高，建议只在大圆角面（输入卡 28、窗口 12）上评估。

---

## 6. 改/验证流程备忘

- **主题 token 热改**：`theme-*.json` 与 `metrics.json` 会被释放到 `<exe>/ui/`，运行时**磁盘优先**，改完重启即生效，不用重编。
- **QML 热改**：同理（`StyleResolver` 磁盘优先、qrc 兜底）。改 `source/qml/**` 后同步一份到 `x64/Release/ui/**` 就能先看效果；最终仍需重编，把新树打进 qrc（首装用户靠 qrc 释放）。
- **`highlight-rules.json`** 是编进 qrc 的（`:/ULinHub/config/highlight-rules.json`），**必须重编**才生效。
- **构建**：
  ```powershell
  & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
      'source\ULin Hub.vcxproj' /p:Configuration=Release /p:Platform=x64 /m /v:minimal
  ```
  构建前必须先关掉正在跑的 `ULin Hub.exe`，否则 LINK 会报 `LNK1104`。
- **抓图/验证**：`build/temp/official-ui/shot.ps1 -Out x.png`（按标题找窗口，`PrintWindow` 抓全内容，不抢前台）；`click-shot.ps1 -X -Y` 先点再抓。注意 PowerShell 5.1 需要 `-ExecutionPolicy Bypass` 跑脚本文件。
- ⚠️ **不要用 PowerShell 的 `Get-Content -Raw` + `Set-Content` 改含中文的 QML**：5.1 默认按 ANSI 解码会把 UTF-8 注释变成乱码（本次踩过一次，已用编辑器重写）。改文本一律用编辑工具。
