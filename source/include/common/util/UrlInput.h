#pragma once

// 地址栏输入的规范化 —— 从 basic/browser/WebBrowserPanel.qml 的 navigate() 搬来。
//
// 为什么进 C++（与仓库既有分层一致）：判 scheme / 判"像不像域名" / 拼搜索词 / 百分号编码
// 都是一次**纯字符串变换**，与窗口、与 WebEngine 都无关。留在 QML 里既没法脱离窗口单测，
// 也会随风格包被拷一份出来各写各的（WebBrowserPanel.qml 是可被风格包覆盖的单元）。
//
// 这里只有自由函数（没有 QObject）：QML 面由 qmlbridge/UrlInputBridge 转调（单例名 `UrlInput`）。

#include <QString>

// 把用户敲进地址栏的一行字变成真正要加载的 URL。
// 规则与搬家前的 JS **逐条对齐**（细节见 UrlInput.cpp）：
//   ① 去掉首尾空白；剩下为空 ⇒ 返回空串（调用方据此"什么都不做"，别去加载空 URL）；
//   ② 匹配 `^[a-zA-Z][a-zA-Z0-9+.-]*:` ⇒ 原样返回（已带 scheme：不补前缀、不做百分号编码）；
//   ③ "含点且无空白" ⇒ 补 `https://`（仍做百分号编码，与搬家前一致）；
//   ④ 其余 ⇒ `https://www.bing.com/search?q=` + 百分号编码（中文按 UTF-8 编码成 %E4%BD%A0…）。
QString normalizeUrlInput(const QString& raw);
