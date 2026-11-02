# 霜玻 Frost

> 玻璃拟物：背后三团环境光，面板是半透明的冷白玻璃片，圆角很大、边很亮。

[浅色预览](previews/light.png) · [深色预览](previews/dark.png)

## 改了什么

| 文件 | 做法 |
|---|---|
| `skin/window/Backdrop.qml` | Canvas 画三团径向渐变光斑（左上冷蓝 / 右上淡蓝 / 底部青色）+ 细颗粒压色带；顶部一道 1px 玻璃高光 |
| `skin/window/TitleBar.qml` | 完全透明的浮层标题，右侧**圆形**窗口键（悬停时亮一层玻璃） |
| `widgets/Card.qml` | 半透明面 + 白高光边 + 同圆角的**竖向渐变**（上白 10~55% → 中透明 → 下暗 2~12%）；阴影仍走 MultiEffect |
| `widgets/ActionButton.qml` | 胶囊 + 竖向玻璃渐变 + 内侧高光边 |
| `skin/chat/UserBubble.qml` | 胶囊气泡（`radius = min(20, height/2)`）+ 同样的玻璃渐变 |
| `skin/session/SessionRow.qml` | 胶囊行（`radius = height/2`）+ 选中更亮一层 + 左侧一小段圆头条 |
| `metrics.json` | 圆角放大（window 16 / card 16 / composer 26）、`shadow.cardBlur=24`、滚动条更细更长 |

## 色板要点

- 透明度是这个包的主角：`panelBg` / `cardBg` / `sessionBarBg` / `composerBg` **全部是 rgba**，
  不这么写就透不出 Backdrop 的光斑，玻璃感会变成灰块。
- light：底 `#E8EEF8`，主按钮 `#3D63C8`；dark：底 `#0E131C`，主按钮亮蓝 `#5B8FF0`。

## 装法

```powershell
Copy-Item -Path "assets\style-packs\frost\*" -Destination "x64\Release\ui\" -Recurse -Force
```

重启客户端。恢复默认：设置 → 外观 → 重置界面包。
