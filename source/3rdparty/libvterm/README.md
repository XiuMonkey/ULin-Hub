# libvterm（内嵌副本）

终端 VT 解析器。**只解析、不渲染**：把字节流按 ECMA-48 / DEC 私有序列解释成"屏幕网格 + 属性"，
通过回调交给宿主，宿主负责把网格画出来。

## 出处与版本锚点

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/neovim/libvterm |
| 分支 | `nvim`（该仓库的默认分支，不是 `master`） |
| 版本宏 | `VTERM_VERSION 0.3.3`（见 `include/vterm.h:14-16`） |
| 抓取日期 | 2026-10-15 |
| 抓取方式 | 按上游目录结构逐个取 raw 文件，未做任何修改 |
| 完整性 | `VENDOR.sha256`（19 条，覆盖 `LICENSE` + `include/` + `src/` 全部上游文件；本 README 与清单自身不在内。在该目录下跑 `sha256sum -c VENDOR.sha256` 可校验） |

## 为什么用 neovim 的 fork 而不是 launchpad 上的原版

原版（Paul "LeoNerd" Evans）最新发布就是 0.3.3（2022-09），之后基本停更，且有几个我们正踩的坑。
neovim 的 fork 在 0.3.3 之上持续修正，本副本用到的、原版没有的能力：

- `VTermGlyphInfo.width`：`putglyph` 回调直接给出字符占**几列**（0 = 宽字符的续格，1/2 = 本格）。
  自绘渲染器靠它才能把 CJK 摆对位置 —— 原版只有 `VTermScreenCell.width`，且不含续格语义。
- `VTermScreenCell.width` 同理，取网格时逐格可得。
- `sb_pushline4(cols, cells, continuation)`：回滚行带"是否上一行的折行续行"标志（`VTermLineInfo.continuation`），
  否则长命令换行后在回滚里会显示成两行。
- `VTERM_UNDERLINE_CURLY`：现代 shell 的波浪下划线。
- `VTermStateCallbacks.premove` / `vterm_state_callbacks_has_premove()`：moverect 前的一次回调。
- 键盘侧的 `VTermModifier` / `VTermKey` 覆盖更全（`vterm_keyboard_key` 生成序列，见下）。

## 为什么内嵌（以及当前没有走这条路）

引擎（`api-terminal-controller`）给客户端的只有**原始 PTY 字节流**，外加一份序列化的 VT 重放串
（`terminal/follow` 的 `snapshot.screen` 是 `@xterm/addon-serialize` 的产物）—— **没有"单元格网格"
接口**。所以"QML 只画格子"这条路在上游不存在：要自己画，就得自己解析。本副本 + 包装类
`source/src/common/terminal/TerminalVt.{h,cpp}` 就是为这条路准备的（字节进、`TermCell` 网格出）。

⚠️ 但**当前跑着的终端面板不走它**：`TerminalBridge` 只做协议状态（mount / attach / 序号 / 背压 /
resize / close），帧经 QWebChannel 推给页面里的 xterm.js 解析并渲染 —— 理由与调查见
`source/include/qmlbridge/TerminalBridge.h` 的头注释、`build/temp/terminal-probe/TERMINAL-REPORT.md`。
`TerminalVt` 目前唯一的消费者是离线自检 `misc/tools/vt-selfcheck/`。
两条路产出的是同一件东西（视口网格），区别只在**谁解析**。

## 目录结构（与上游一致，便于日后整目录 diff 升级）

```
include/vterm.h              公开 API 主头（要加进 include 路径的**目录**就是 include/）
include/vterm_keycodes.h     VTermKey / VTermModifier 等键码枚举，由 vterm.h 包含
src/*.c                      9 个编译单元：encoding keyboard mouse parser pen screen state unicode vterm
src/*.h, src/*.inc           内部头与查表数据（fullwidth.inc = 宽字符表）
src/encoding/*.inc,*.tbl     DEC 线绘 / UK 字符集查表（.inc 是已生成好的，不需要生成器）
LICENSE                      MIT
VENDOR.sha256                本副本的完整性清单（19 条，不含本 README 与自身）
```

编译单元清单（9 个，C99，零外部依赖）：
`encoding.c` `keyboard.c` `mouse.c` `parser.c` `pen.c` `screen.c` `state.c` `unicode.c` `vterm.c`

## 怎么用（本项目里的接法）

**只加 `include/` 到头文件搜索路径**，`src/` 里的 `#include "vterm_internal.h"` 等是相对同目录的。
编译时**必须按 C 编译**，不要被工程级的 `/TP`、`stdcpp17`、`ConformanceMode` 带过去 —— libvterm 是 C99，
用 C++ 规则编译会因隐式转换、`void*` 转换报错。**光靠 `.c` 后缀不够**：工程 ItemDefinitionGroup 里的
`ConformanceMode` / `LanguageStandard` 是继承下来的，所以本工程显式写了
`<CompileAs>CompileAsC</CompileAs>`。

另有一处必须在**构建侧**替这 9 个文件放行，原因是工程的 `SDLCheck`（`/sdl`）会把两个警告抬成**错误**
（工程里并没有 `/WX`，就是 `/sdl` 干的；实测 `/WX-` 撤不掉这个升级，只能关 `/sdl`）：

| 位置 | 诊断 | 内容 |
|---|---|---|
| `parser.c:53` | C4996 | `strncpy` 弃用 |
| `parser.c:183` | C4703 | `string_start` 可能未初始化 |

做法：`.vcxproj` 里这 9 条 `<ClCompile>` 各带 `<SDLCheck>false</SDLCheck>` +
`<PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions>` +
`<TreatWarningAsError>false</TreatWarningAsError>`（最后一条是给 `state.c:319/326` 的 C4267 普通警告
留的余地：第三方代码不随我们修，别让日后的 `/WX` 挡住）。CMake 侧对应
`set_source_files_properties(... COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)` —— CMake 的 MSVC
默认参数里没有 `/sdl`，所以那边不会踩这条。

**别去改 `parser.c` 的那处 `strncpy`** —— 它的上下文本身是安全的（`char seq[INTERMED_MAX+1]`，
长度只取 `intermedlen`≤2，NUL 是下一行手动补的），C4703 那处也是控制流上必被赋值的；动它只会破坏
与上游的一致性，升级时要再处理一遍。

自查这套开关的脚本：`misc/tools/vt-selfcheck/run.ps1`（编译开关已与上表对齐：
`/W3 /sdl- /D_CRT_SECURE_NO_WARNINGS`）。**改开关时两边一起改** —— 少了 `/sdl-`，自检会"过"
而真实构建挂（这个假阳性真发生过一次）。

本工程的接入点：`source/3rdparty/libvterm`（vcxproj 手工列 + CMake GLOB 自动收）。

## 已知边界（用之前先知道，别踩）

- **UTF-8 跨 chunk 由库自己续上 —— 不需要外部拼帧**：解码器的 `bytes_remaining` / `bytes_total` /
  `this_cp` 存在 `VTermState` 的 `encoding[i].data` 里（`vterm_internal.h:126` 的
  `VTermEncodingInstance encoding[4], encoding_utf8`），跨 `vterm_input_write()` 调用一直保持；
  只有"指定字符集"（`state.c:701`）和"状态重置"（`state.c:2141`）才会调 `init` 把它清零。
  所以**引擎逐 chunk 给的 `output` 帧可以直接喂**，一个多字节字符被拆到两次调用里也不会解成 U+FFFD。
  转义序列同理：`vt->parser.state` 也挂在 VTerm 上持久（`parser.c`）。
- **回滚缓冲不在库里**：屏幕只有当前页，`sb_pushline` 是回调，滚回存储由宿主自己实现。
- **`vterm_screen_flush_damage()` 的语义**：damage 回调是"哪块变了"的提示，用来触发重绘，
  不代表格子里已经是最终值 —— 渲染前要 `vterm_screen_get_cell()` 取真值。

## 升级方式

整目录替换后重跑 `VENDOR.sha256`。升级前先看 `vterm_internal.h` 里的内部结构有没有动
（本副本只依赖公开 API，但 `VTermScreenCell` 布局变化会影响我们的网格拷贝代码）。
