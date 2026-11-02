# 苏黎世 Swiss

> 国际主义版式：纯白纸、纯黑线、一点红。零圆角、零阴影，层次全靠线与留白。

[浅色预览](previews/light.png) · [深色预览](previews/dark.png)

## 改了什么

| 文件 | 做法 |
|---|---|
| `skin/window/Backdrop.qml` | Canvas 画 48px 版面网格 + 12px 细分；左上 10px 红方块、右下 28×4 红条作为定位标记 |
| `skin/window/TitleBar.qml` | 粗黑产品名（字距 −0.2）+ 左侧红方块标 + 底沿 **2px** 黑基线；窗口键直角，按下反白 |
| `widgets/Card.qml` | 0 圆角、**2px 硬描边**、零阴影，左上角一个 6px 红角标 |
| `widgets/ActionButton.qml` | 方形实块：primary 纯黑反白、**悬停变红**；elevated 白底黑框 |
| `skin/chat/UserBubble.qml` | 矩形框 + 2px 黑边 + 左沿红块（引用标记） |
| `skin/session/SessionRow.qml` | 直角色块行：选中**整行反白**（黑底白字）+ 左沿 3px 红条 |
| `metrics.json` | 全直角、`shadow.*Blur=0`、滚动条 12px、面板栏 320、留白更大（cardPadding 20） |

## 色板要点

- light 只有三个颜色：`#FFFFFF` / `#111111` / `#E03131`。刻意不做灰阶过渡，靠 2px 线分级。
- dark 完全反转：底 `#0C0C0C`、面 `#171717`、线 `#3D3D3D`、红升到 `#FF4D4D`。

## 装法

```powershell
Copy-Item -Path "assets\style-packs\swiss\*" -Destination "x64\Release\ui\" -Recurse -Force
```

重启客户端。恢复默认：设置 → 外观 → 重置界面包。
