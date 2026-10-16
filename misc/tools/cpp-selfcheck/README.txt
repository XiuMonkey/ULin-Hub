C++ 侧自检（搬家后的解析 / 模型 / 策略）：不依赖 QML 与引擎，直接链接被测 .cpp。

为什么要它：这个仓库原来没有 C++ 单测，凡"能被字符串/数字独立描述"的逻辑搬进 C++ 后，
就能像这样脱离窗口验证（也正因为如此才把它们从 QML 搬出来）。

领域归位（2026-10-15）之后，被测对象都在 common/**：UrlInput（common/util）、
PanelDockModel（common/shell）、PanelBarMetrics（common/layout，自由函数）；
qmlbridge/PanelBarLayout 只剩转发，所以自检额外核对"单例转发层与 PanelBarMetrics 逐位一致"。

跑法（一键，推荐）：
  powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\cpp-selfcheck\run.ps1
  · 可传 -QtDir / -VsMsvcDir / -SdkDir / -SdkVersion 覆盖工具链路径（默认值见脚本头部）；
  · 产物落在 <仓库>\build\cpp-selfcheck\（build/ 已被 .gitignore 忽略），不脏 source/；
  · ⚠️ run.ps1 本身存成 UTF-8 **带 BOM**（与 misc\tools\*.ps1 一致）：Windows PowerShell 5.1
    读无 BOM 的 .ps1 会按 ANSI/GBK 解，中文注释会把字符串引号吃坏、脚本直接解析失败。

跑法（手动，等价于脚本里那两段。$q = Qt 目录，$d = 仓库根）：
  $q\bin\moc.exe $d\source\include\common\shell\PanelDockModel.h -o moc_PanelDockModel.cpp
  $q\bin\moc.exe $d\source\include\qmlbridge\PanelBarLayout.h    -o moc_PanelBarLayout.cpp
  cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /MD /O2 ^
     /I"$q\include" /I"$q\include\QtCore" /I"$d\source\include" ^
     selfcheck.cpp $d\source\src\common\util\UrlInput.cpp ^
     $d\source\src\common\shell\PanelDockModel.cpp ^
     $d\source\src\common\layout\PanelBarMetrics.cpp ^
     $d\source\src\qmlbridge\PanelBarLayout.cpp ^
     moc_PanelDockModel.cpp moc_PanelBarLayout.cpp /Fe:selfcheck.exe ^
     /link /LIBPATH:"$q\lib" Qt6Core.lib
  （/utf-8 不能省：源文件里有中文注释且没有 BOM。）

覆盖：
  ① UrlInput 规范化 8 个输入；
  ② PanelDockModel 的 open/close/showHome（count / 同类标题编号 / activeIndex 收敛 / homeVisible）；
  ③ PanelBarMetrics 的会话区保底、上限、两端夹取与拖动换算（往左 50 ⇒ 宽度 +50，1:1 跟手），
     外加 PanelBarLayout 单例（QML 契约面）与自由函数逐位一致。
输出为逐条 PASS/FAIL，末尾汇总；全 PASS 时退出码 0。
