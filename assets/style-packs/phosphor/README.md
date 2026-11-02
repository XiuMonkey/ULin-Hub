# 荧光屏 Phosphor

> CRT 终端：纯黑磷光屏、荧光绿文字、扫描线与暗角，全直角、四角 L 形角标。

[浅色预览](previews/light.png) · [深色预览](previews/dark.png)

## 改了什么

| 文件 | 做法 |
|---|---|
| `skin/window/Backdrop.qml` | Canvas 画每 3px 一条的扫描线 + 屏幕中心径向微光 + 四角暗角 |
| `skin/window/TitleBar.qml` | 等宽字 `▚ ULin Hub`、字距 1.4，右侧直角窗口键，底沿一条 28% 的磷光线 |
| `widgets/Card.qml` | 纯直角、无阴影；靠 1px 磷光描边 + **四角 L 形角标**（4 根 1px/10px 细条）立住 |
| `widgets/ActionButton.qml` | 终端命令块：直角、左沿 3px 状态条、等宽字；primary 是满格荧光块反白 |
| `widgets/AppScrollBar.qml` | 直角电位器式滚动条（磷光色，三态：28% / 55% / 100%） |
| `skin/chat/UserBubble.qml` | 直角命令行块：左侧 `>` 提示符 + 2px 荧光条，正文等宽 |
| `skin/session/SessionRow.qml` | 直角行、选中加左沿贯通条、运行中是**会呼吸的方块光标**（620ms 步进闪烁） |
| `metrics.json` | 全直角（radius 0~2）、字号缩小一档、`shadow.*Blur=0`、动效 60/120/200ms |

## 色板要点

- dark 是主设定：底 `#070A08`，面板 `#0B0F0C`，文字 `#C9FFDF`，强调荧光绿 `#3DFF9E`。
- light 不是"浅色终端"，而是**点阵打印机版**：白底、墨绿字、形状一模一样（同一套 QML）。

## 装法

```powershell
Copy-Item -Path "assets\style-packs\phosphor\*" -Destination "x64\Release\ui\" -Recurse -Force
```

重启客户端。恢复默认：设置 → 外观 → 重置界面包。
