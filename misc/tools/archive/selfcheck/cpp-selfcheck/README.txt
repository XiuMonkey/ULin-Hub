C++ 侧自检（搬家后的解析 / 模型 / 规则）：不依赖 QML 与引擎，直接链接被测 .cpp。

为什么要它：这个仓库原来没有 C++ 单测，凡"能被字符串/数字独立描述、且 C++ 侧真有消费者"的逻辑
搬进 C++ 后，就能像这样脱离窗口验证。

被测对象（都在 common/**）：
  UrlInput（common/util）、PanelDockModel（common/shell）、CodeHighlighter（common/util）。

⚠️ 右侧栏宽度那三条公式（会话区保底 / 上限 / 夹取）**不在本自检范围**：它们唯一的消费者是
Shell.qml，2026-10-18 已从 common/layout + qmlbridge/PanelBarLayout 归位回 QML
（source/qml/basic/shell/Shell.qml 的 conversationMinWidth / maxWidth / clampWidth）。
原来那几条断言（convMin==308、dragL==350、clamp 到 260）只是 min/max 的常数点，
真正的风险点（拖柄喂的是 panelBarEffW 还是 panelBarW）一条也没覆盖 ⇒ 不另建 QML 测试基座。

跑法（一键，推荐）：
  powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\cpp-selfcheck\run.ps1
  · 可传 -QtDir / -VsMsvcDir / -SdkDir / -SdkVersion 覆盖工具链路径（默认值见脚本头部）；
  · 产物落在 <仓库>\build\cpp-selfcheck\（build/ 已被 .gitignore 忽略），不脏 source/；
  · ⚠️ run.ps1 本身存成 UTF-8 **带 BOM**（与 misc\tools\*.ps1 一致）：Windows PowerShell 5.1
    读无 BOM 的 .ps1 会按 ANSI/GBK 解，中文注释会把字符串引号吃坏、脚本直接解析失败。

跑法（手动，等价于脚本里那几段。$q = Qt 目录，$d = 仓库根）：
  $q\bin\moc.exe $d\source\include\common\shell\PanelDockModel.h -o moc_PanelDockModel.cpp
  cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /MD /O2 ^
     /I"$q\include" /I"$q\include\QtCore" /I"$d\source\include" ^
     selfcheck.cpp $d\source\src\common\util\UrlInput.cpp ^
     $d\source\src\common\util\CodeHighlighter.cpp ^
     $d\source\src\common\shell\PanelDockModel.cpp ^
     moc_PanelDockModel.cpp /Fe:selfcheck.exe ^
     /link /LIBPATH:"$q\lib" Qt6Core.lib
  （/utf-8 不能省：源文件里有中文注释且没有 BOM。）

覆盖：
  ① UrlInput 规范化 8 个输入；
  ② PanelDockModel 的 open/close/showHome（count / 同类标题编号 / activeIndex 收敛 / homeVisible）
     + 视图表（kind → 视图路径 / blockScoped / 兜底文案）+ activeViewChanged 信号（关前面那块也要发）；
  ③ CodeHighlighter 的规则文件装载、PCRE2 兼容性、别名归一。
输出为逐条 PASS/FAIL，末尾汇总；全 PASS 时退出码 0。
