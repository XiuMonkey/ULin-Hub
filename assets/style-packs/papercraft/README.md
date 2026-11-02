# 素纸 Papercraft

> 把客户端做成一本纸页上的工作簿：宣纸底、稿纸方格、墨线收边、印章式按压。

[浅色预览](previews/light.png) · [深色预览](previews/dark.png)

## 改了什么

| 文件 | 做法 |
|---|---|
| `skin/window/Backdrop.qml` | Canvas 画 32px 稿纸方格 + 固定种子的纸纤维短线；标题栏下沿一道压痕线 |
| `skin/window/TitleBar.qml` | 标题改成**居中版式**（`U L I N H U B`，字距 1.2），左侧只留方标，右侧方角窗口键 + 底沿墨线 |
| `widgets/Card.qml` | 直角纸片 + 1px 墨线 + **位移式硬投影**（整块偏移 `shadowDy`，不模糊）；`shadowEnabled` 时不再走 MultiEffect |
| `widgets/ActionButton.qml` | 方角 + 字距 0.4 + 按下时整块（面与字）下沉 1px，像印章压下去 |
| `skin/chat/UserBubble.qml` | 纸片方角 + 1px 墨线 + 右沿一道赭石**批注条** |
| `skin/session/SessionRow.qml` | 直角行 + 选中时左沿一条**贯通墨条** + 标题加 700 字重 |
| `metrics.json` | 圆角压小（window 8 / card 3 / control 3）、`shadow.cardBlur=0`、字距更松、动效更快 |

## 色板要点

- 底：`#F2ECDE`（宣纸），面板 `rgba(251,246,234,0.88)` —— **半透**，好让纸纹透到侧栏上。
- 墨：`#2C2419`；次文字 `#6A5C48`；强调 `#B0632F`（赭石）。
- 深色是"夜读牛皮纸"：底 `#211C16`，强调升到 `#DB8F57`，主按钮反过来是浅纸底 + 深字。

## 装法

```powershell
Copy-Item -Path "assets\style-packs\papercraft\*" -Destination "x64\Release\ui\" -Recurse -Force
```

重启客户端。恢复默认：设置 → 外观 → 重置界面包。
