C++ 侧自检（搬家后的解析/模型/策略）：不依赖 QML 与引擎，直接链接被测 .cpp。

为什么要它：这个仓库原来没有 C++ 单测，凡"能被字符串/数字独立描述"的逻辑搬进 C++ 后，
就能像这样脱离窗口验证（也正因为如此才把它们从 QML 搬出来）。

跑法（PowerShell，$q = Qt 目录，$d = 仓库根）：
  $q\bin\moc.exe $d\source\include\qmlbridge\PanelDockModel.h -o moc_PanelDockModel.cpp
  $q\bin\moc.exe $d\source\include\qmlbridge\SidebarLayout.h   -o moc_SidebarLayout.cpp
  cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /MD /O2 ^
     /I"$q\include" /I"$q\include\QtCore" /I"$d\source\include" ^
     selfcheck.cpp $d\source\src\common\util\UrlInput.cpp ^
     $d\source\src\qmlbridge\PanelDockModel.cpp $d\source\src\qmlbridge\SidebarLayout.cpp ^
     moc_PanelDockModel.cpp moc_SidebarLayout.cpp /Fe:selfcheck.exe ^
     /link /LIBPATH:"$q\lib" Qt6Core.lib

覆盖：① UrlInput 规范化 8 个输入；② PanelDockModel 的 open/close/showHome（count/标题编号/activeIndex/homeVisible）；
③ SidebarLayout 的保底、上限、拖动换算（往左 50 ⇒ 宽度 +50，1:1）与两端夹取。
输出为逐条 PASS/FAIL，末尾汇总；全 PASS 时退出码 0。
