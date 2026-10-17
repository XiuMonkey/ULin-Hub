# 示例皮肤：壁纸

> 这是**等价复刻原版 QSS 壁纸包**的 QML 版。原版 = `misc/ReleaseBuild/StyleExample/Bg-Genshin-Keqing.zip`
> （`assets/StyleExample/Bg-Genshin-Keqing/` 目录已删，zip 留作参照）。

## 装法

把本目录的文件按同样的相对路径拷进 `<exe>\styles\`，重启应用：

```cmd
robocopy . "<仓库根>\x64\Release\styles" /E
```

恢复默认：设置 → 外观 → **重置 QML**，或删掉 `<exe>\styles\`（下次启动重新释放内置包）。

打包副本（与旧 QSS 包同样的分发形态，内容在压缩包根）：
`misc\ReleaseBuild\StyleExample\Qml-Wallpaper.zip`。

## 文件

| 文件 | 作用 |
|---|---|
| `ui/appearance/window/Backdrop.qml` | 壁纸层（含圆角遮罩、描边） |
| `theme-light.json` / `theme-dark.json` | **完整色板**（56 键），只改了 3 个值 |
| `wallpaper.png` | 1600×1000，alpha 0.45（原版那张原图） |

## 与原版逐项对应

| 原版 QSS | 这里 | 值（light / dark） |
|---|---|---|
| `#dshhubCentral { background-color: {{wallpaperMask}} }` | `windowBg` | `#FFFFFF` / `#111827` |
| `border-image: url(...) 0 0 0 0 stretch stretch` | `Backdrop.qml` 的 `Image { fillMode: Image.Stretch }` | 同一张 `wallpaper.png` |
| `#topBar`、`#sidebar { background: {{wallpaperPanelBg}} }` | `cardBg` | `rgba(255,255,255,0.72)` / `rgba(31,41,55,0.62)` |
| `#inputCapsule { background: {{wallpaperInputBg}} }` | `composerBg`（输入卡专属键） | `rgba(255,255,255,0.78)` / `rgba(31,41,55,0.70)` |
| `#dshhubCentral { border: 1px solid {{windowBorder}} }`，但被 `border-image` 盖住（视觉无描边） | 不画描边 | — |
| Agent 气泡、设置页、插件市场**原版没动** | `panelBg` 保持内置不透明值 | — |

## ⚠️ 色板必须写完整份（最容易踩的坑）

`ThemeManager::loadPaletteFor()` 是**整文件替换**，不做逐键合并：

- 只写你改的那几个键 → **其余键全部缺失** → QML 里 `Theme.color.border` 拿到 `undefined`
  → 颜色属性赋值失败后**取默认值黑色**。
- 症状：**每个部件一圈黑边、按钮变黑**（我第一次就是这么踩的）。
- 做法：把 `assets/resources/styles/theme-light.json`（内置 55 键）**整份**拷过来再改值。
  本示例额外新增了 `composerBg`，所以是 56 键。

## 已知偏差

1. 卡片自身的投影（`Card.qml` 的 `MultiEffect`）会画到卡片**底下**，卡片半透明时这层阴影会透上来：
   浅色主题约暗 3%（shadow 12% 黑），深色主题约暗 11%（shadow 30% 黑），边缘最浓。
   旧壳的 `CardShadow` 是"留白反推"（阴影不落在面板底下），所以原版没这个现象。
   修法：给 `MultiEffect` 加反相遮罩（`maskInverted: true` + 卡片形状当遮罩），让阴影只画在卡片之外。
2. 壁纸用 `Stretch`（与原版 `stretch stretch` 一致）会轻微变形；想保持比例就换
   `Image.PreserveAspectCrop`（但会裁掉上下，原版不是这样）。

## 其它边界

- **只用 PNG**：发布包没有 `imageformats/`，JPEG 会**静默不显示**。
- **背景层不得接受鼠标事件**（别放 `MouseArea`），否则整窗点不动。
- **最大化**时圆角为 0（`edgeToEdge`），皮肤自己的绘制也要跟着关。
- **壁纸路径**：`Image { source: "x.png" }` 的相对路径按 QML 文件自身目录解析；
  引用包根目录要写 `Style.url("x.png")`（本示例用的后者）。
