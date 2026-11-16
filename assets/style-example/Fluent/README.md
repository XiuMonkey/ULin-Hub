# Fluent · Win11 云母

> 按 **Fluent 2 / WinUI 3** 做的风格包：Mica 底、窗口 12px / 表面 10px / 控件 6px 圆角、
> 1px 细描边、tonal elevation（不靠重阴影分层）。比 Win11 原生的 8/8/4 圆一档，观感更柔和。
> 目标是与 Windows 11 原生窗口放在一起不违和。

**`preview.html` 是定稿样张**（单文件 HTML/CSS，双击用 Edge 打开，可切深浅色与「会话/设置」两视图）。
QML 里的数值与它一一对应；以后改风格先改样张，再同步进包。

## 装法

```powershell
Copy-Item -Path "assets\style-example\Fluent\*" -Destination "x64\Release\resources\ui\" -Recurse -Force
```

安装目录同理：目标 `D:\Program Files\ULinHub\resources\ui\`。
重启客户端。恢复默认：设置 → 外观 → 重置界面包。

## 设计对照（Win11 规范 → 本包做法）

| Win11 规范 | 本包 |
|---|---|
| Mica：桌面壁纸的低饱和着色 + 顶部 rim | `Backdrop.qml`：**浅一档**蓝调渐变 #F9FAFD→#E9EFF9（深 #232428→#1A1B20）+ 左上蓝 0.10 / 右下紫 0.07 薄雾 + 2~4% 噪点 + 顶部 1px rim |
| Acrylic：只给浮层（菜单/弹窗） | `menuBg` 用 rgba(252,252,253,0.92) / rgba(42,43,48,0.94) |
| 原生圆角 8/8/4；本包柔和一档 | `metrics.json`：window 12 / panel·card·composer 10 / control·input·chip 6 |
| 发丝级描边（stroke），不用重阴影 | `border` = rgba(16,32,64,0.062) / rgba(255,255,255,0.072)；卡片默认无阴影 |
| tonal elevation：背景色阶分层 | **实面卡片** `cardBg` = #FFFFFF / #2A2B30（不是玻璃），`hoverBg` / `activeBg` 做交互层级 |
| 强调色：浅 #0066CC、深 #60CDFF；深色强调按钮用近黑字 | `accent` / `primaryButton*` 对应 |
| 窗口键 46×32、贴右缘；关闭悬停红 #C42B1C | `TitleBar.qml`（Segoe Fluent Icons 字形 E921/E922/E923/E8BB） |
| NavigationView 选中 = 左侧 3px 强调条 | `SessionRow.qml` |
| 动效 ControlFast/Normal/Slow = 150/250/350ms | `motion` 三档 |

## 文件

| 文件 | 做法 |
|---|---|
| `preview.html` | 定稿样张（设置页 + 会话页 + 深浅色），QML 的视觉依据 |
| `skin/window/Backdrop.qml` | Mica 近似：蓝调渐变 #F2F5FC→#DDE6F4 + 噪点（无光晕）；圆角裁剪 + 1px 描边 |
| `skin/shell/SidebarSurface.qml` | 侧栏去卡片：透明底 + 右侧发丝线（与背景共用） |
| `skin/shell/TopBarSurface.qml` | 顶栏去卡片：透明底 + 底部发丝线 |
| `skin/window/TitleBar.qml` | Win11 标题栏：12px 次要色标题、46×32 方形窗口键、关闭悬停红 |
| `widgets/Card.qml` | 1px 描边 + 半透明面，无渐变、无默认阴影 |
| `widgets/ActionButton.qml` | Win11 按钮：6px 圆角；次要按钮半透明面 + 1px 描边；主按钮强调色填充 |
| `skin/session/SessionRow.qml` | NavigationView 式导航项（左侧 3px 强调条，36px 行高） |
| `metrics.json` | 圆角/间距/尺寸/阴影/动效按定稿数值 |
| `theme-light.json` / `theme-dark.json` | 完整 74 键色板（浅底 #F9FAFD→#E9EFF9 / 深 #232428→#1A1B20，实面卡片） |

## 已知限制

- Mica 需要采样桌面壁纸，插件层拿不到系统接口，`Backdrop` 用双色光斑近似——观感接近，但不会随壁纸颜色变化。
- `Segoe Fluent Icons` 为 Windows 11 自带字体；在 Windows 10 上窗口键字形会回退成方块，可把 `TitleBar.qml` 里的字形换回 `− □ ✕`。
