# 樱岚 Sakura

> 晨雾粉的渐变底，花瓣随风落在留白里；控件是圆润的胶囊，强调色是玫瑰。

[浅色预览](previews/light.png) · [深色预览](previews/dark.png)

## 改了什么

| 文件 | 做法 |
|---|---|
| `skin/window/Backdrop.qml` | Canvas 画竖向玫瑰渐变（顶部 10% → 中段透明 → 底部 6%）+ 用 `quadraticCurveTo` 拼的五瓣小花（固定种子，位置稳定） |
| `skin/window/TitleBar.qml` | 左对齐柔光标题（产品名取玫瑰色）+ 圆形窗口键 + 一条 14% 的粉分隔线 |
| `widgets/Card.qml` | 大圆角奶白片 + 左上角渐亮（花瓣边的受光感）+ 柔和 MultiEffect 阴影 |
| `widgets/ActionButton.qml` | 玫瑰渐变胶囊，悬停整块提亮 |
| `skin/chat/UserBubble.qml` | 圆润胶囊气泡 + 左侧一枚**三瓣小花点** |
| `skin/session/SessionRow.qml` | 胶囊行；选中时左侧显一朵小花（三瓣叠圆 + 花芯） |
| `metrics.json` | 圆角放大（card 16 / composer 26）、留白更松、`shadow.cardBlur=26`、动效 130/240/360ms |

## 色板要点

- light：底 `#FFF5F7`，主按钮 `#C94A72`，气泡 `#FCE4EC`。
- dark：底 `#1B1216`（深梅），强调升到 `#F58AAE`，避免粉色在暗底上发灰。

## 装法

```powershell
Copy-Item -Path "assets\style-packs\sakura\*" -Destination "x64\Release\ui\" -Recurse -Force
```

重启客户端。恢复默认：设置 → 外观 → 重置界面包。
