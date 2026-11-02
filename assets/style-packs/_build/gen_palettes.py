"""风格包色板生成器（构建期工具，不进包）。

每个风格包 = 一份语义参数表（light / dark 各一组），由本脚本展开成采集器要的完整
76 键调色板（key 顺序与内置 assets/resources/styles/theme-*.json 一致）。

为什么用参数表而不是手写 76 个键：ThemeBridge 是**整文件替换**、不逐键合并，漏一个键
QML 里就拿到 undefined、颜色退回黑色（黑边/黑按钮）。展开层保证永不漏键。

用法：python gen_palettes.py            # 写回各包目录
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILTIN = os.path.join(ROOT, "..", "resources", "styles")

# key -> 参数名。sendButton* 与主按钮同族，tagBg 跟着次要按钮面走。
KEYMAP = [
    ("windowBg", "window"),
    ("panelBg", "panel"),
    ("composerBg", "composer"),
    ("cardBg", "card"),
    ("hoverBg", "hover"),
    ("activeBg", "active"),
    ("border", "border"),
    ("textPrimary", "text1"),
    ("textSecondary", "text2"),
    ("accent", "accent"),
    ("accentHover", "accentHover"),
    ("primaryButtonBg", "primaryBg"),
    ("primaryButtonHover", "primaryHover"),
    ("primaryButtonPressed", "primaryPressed"),
    ("primaryButtonText", "primaryText"),
    ("primaryButtonDisabledBg", "disabledBg"),
    ("primaryButtonDisabledText", "disabledText"),
    ("danger", "danger"),
    ("dangerBg", "dangerBg"),
    ("inputBg", "input"),
    ("userBubbleBg", "userBubble"),
    ("userBubbleText", "userText"),
    ("scrollbar", "scroll"),
    ("scrollbarHover", "scrollHover"),
    ("inputBorder", "inputBorder"),
    ("buttonBg", "buttonBg"),
    ("buttonHover", "buttonHover"),
    ("buttonPressed", "buttonPressed"),
    ("scrollbarHandle", "scrollHandle"),
    ("scrollbarHandleHover", "scrollHandleHover"),
    ("sendButtonHover", "primaryHover"),
    ("sendButtonPressed", "primaryPressed"),
    ("toastBg", "toast"),
    ("overlayBg", "overlay"),
    ("tagBg", "tag"),
    ("dangerButtonBg", "danger"),
    ("dangerButtonText", "onDanger"),
    ("selectionBg", "selection"),
    ("iconButtonText", "iconText"),
    ("iconButtonTextHover", "iconTextHover"),
    ("iconButtonHoverBg", "iconHover"),
    ("iconButtonPressedBg", "iconPressed"),
    ("loadingText", "text2"),
    ("loadingBorder", "inputBorder"),
    ("systemMessageText", "text3"),
    ("shadow", "shadow"),
    ("shadowSubtle", "shadowSubtle"),
    ("shadowFloat", "shadowFloat"),
    ("textOnAccent", "onAccent"),
    ("composerBorder", "composerBorder"),
    ("textTertiary", "text3"),
    ("textCaption", "text4"),
    ("chipHoverBg", "iconHover"),
    ("menuBg", "menu"),
    ("menuBorder", "menuBorder"),
    ("windowBorder", "windowBorder"),
    ("captionHoverBg", "active"),
    ("sessionBarBg", "sessionBar"),
    ("codeBlockBg", "codeBg"),
    ("codeBlockBannerBg", "codeBanner"),
    ("inlineCodeBg", "inlineCode"),
    ("trackBg", "track"),
    ("indicatorBg", "indicator"),
    ("ringBg", "ring"),
    ("dangerText", "dangerText"),
    ("linkText", "accentHover"),
    ("toolbarBg", "toolbar"),
]

SYNTAX_KEYS = ["syntaxKeyword", "syntaxString", "syntaxComment", "syntaxConstant",
               "syntaxFunction", "syntaxVariable", "syntaxPunct"]


def pack_spec(**kw):
    """补默认值：未写的参数按语义就近取，保证 76 键齐全。"""
    s = dict(kw)
    s.setdefault("composer", s["card"])
    s.setdefault("tag", s["buttonBg"])
    s.setdefault("menu", s["card"])
    s.setdefault("menuBorder", s["border"])
    s.setdefault("toolbar", s["buttonBg"])
    s.setdefault("sessionBar", s["panel"])
    s.setdefault("codeBg", s["card"])
    s.setdefault("codeBanner", s["codeBg"])
    s.setdefault("inlineCode", s["codeBg"])
    s.setdefault("indicator", s["card"])
    s.setdefault("ring", s["border"])
    s.setdefault("track", s["hover"])
    s.setdefault("onDanger", "#FFFFFF")
    s.setdefault("dangerText", s["danger"])
    s.setdefault("disabledBg", s["buttonBg"])
    s.setdefault("disabledText", s["text4"])
    s.setdefault("scrollHandle", s["scroll"])
    s.setdefault("scrollHandleHover", s["scrollHover"])
    s.setdefault("iconPressed", s["iconHover"])
    return s


# ---------------------------------------------------------------- 主题定义
# 1) 素纸 papercraft：宣纸底 + 细网格 + 硬投影 + 印刷体，暖赭石强调。
PAPER_LIGHT = pack_spec(
    window="#F2ECDE", panel="rgba(251,246,234,0.88)", card="#FFFCF3", composer="#FFFCF3",
    hover="#EDE4D1", active="#E5DAC2", border="rgba(93,76,52,0.16)",
    text1="#2C2419", text2="#6A5C48", text3="#8B7C66", text4="#A2937C",
    accent="#B0632F", accentHover="#8E4B20",
    primaryBg="#3A3126", primaryHover="#4F4433", primaryPressed="#292117", primaryText="#FBF6EA",
    danger="#B44A34", dangerBg="#F7E5DD", dangerText="#A03B27", onDanger="#FFF9F0",
    input="#FFFCF3", inputBorder="rgba(93,76,52,0.26)", composerBorder="rgba(93,76,52,0.28)",
    userBubble="#EADFC4", userText="#2C2419",
    buttonBg="#EDE4D1", buttonHover="#E5DAC2", buttonPressed="#DCCFB2", tag="#E7DCC4",
    scroll="#D9CAA8", scrollHover="#C4B18B",
    toast="#3A3126", overlay="rgba(44,36,25,0.34)", selection="#E2CDA2",
    iconText="#6A5C48", iconTextHover="#2C2419",
    iconHover="rgba(93,76,52,0.08)", iconPressed="rgba(93,76,52,0.14)",
    shadow="rgba(60,45,25,0.16)", shadowSubtle="rgba(60,45,25,0.09)", shadowFloat="rgba(60,45,25,0.24)",
    onAccent="#FFF9F0",
    menu="#FFFCF3", menuBorder="rgba(93,76,52,0.20)", windowBorder="rgba(93,76,52,0.26)",
    sessionBar="rgba(246,240,226,0.86)", codeBg="#F7F1E2", codeBanner="#F0E8D5", inlineCode="#F1E9D7",
    track="rgba(93,76,52,0.10)", ring="rgba(93,76,52,0.18)", toolbar="#EDE4D1",
    syntax=["#A6402F", "#4F6B39", "#9A8F7B", "#2E5F79", "#6B4C86", "#95551F", "#5C5344"],
)

PAPER_DARK = pack_spec(
    window="#211C16", panel="rgba(42,36,28,0.90)", card="#2F281F", composer="#2F281F",
    hover="#362E24", active="#3F362A", border="rgba(255,238,206,0.09)",
    text1="#F2E7D5", text2="#C6B69C", text3="#A2917A", text4="#82745F",
    accent="#DB8F57", accentHover="#EDA871",
    primaryBg="#F2E7D5", primaryHover="#FFF8EB", primaryPressed="#DDCEB6", primaryText="#2C2419",
    disabledBg="#3A3227", disabledText="#7C6E5A",
    danger="#D97D62", dangerBg="#3B231A", dangerText="#E59678", onDanger="#2C1109",
    input="#262019", inputBorder="rgba(255,238,206,0.14)", composerBorder="rgba(255,238,206,0.16)",
    userBubble="#3B3226", userText="#F2E7D5",
    buttonBg="#362E24", buttonHover="#3F362A", buttonPressed="#483D2E", tag="#3B3226",
    scroll="#4B4234", scrollHover="#5D5343",
    toast="#483D2E", overlay="rgba(10,8,5,0.55)", selection="rgba(219,143,87,0.32)",
    iconText="#B7A78D", iconTextHover="#F2E7D5",
    iconHover="#362E24", iconPressed="#3F362A",
    shadow="rgba(0,0,0,0.45)", shadowSubtle="rgba(0,0,0,0.28)", shadowFloat="rgba(0,0,0,0.58)",
    onAccent="#2C1109",
    menu="#362E24", menuBorder="rgba(255,238,206,0.12)", windowBorder="rgba(255,238,206,0.14)",
    sessionBar="rgba(42,36,28,0.86)", codeBg="#241E18", codeBanner="#2C251C", inlineCode="#2A231B",
    track="rgba(255,238,206,0.10)", ring="rgba(255,238,206,0.16)", toolbar="#362E24",
    syntax=["#E9947A", "#A3C48A", "#94896F", "#7FB8D8", "#C2A0E0", "#E0A468", "#B4A88F"],
)

# 2) 霜玻 frost：冷蓝底 + 彩色光斑 + 半透明玻璃面 + 大圆角。
FROST_LIGHT = pack_spec(
    window="#E8EEF8", panel="rgba(255,255,255,0.62)", card="rgba(255,255,255,0.72)",
    composer="rgba(255,255,255,0.80)", hover="rgba(255,255,255,0.55)", active="rgba(226,236,251,0.85)",
    border="rgba(255,255,255,0.75)", text1="#14203A", text2="#4B5B78", text3="#6E7C96", text4="#8C99B2",
    accent="#4A78E6", accentHover="#3563D6",
    primaryBg="#3D63C8", primaryHover="#3457B4", primaryPressed="#2A4899", primaryText="#F7FAFF",
    disabledBg="rgba(255,255,255,0.46)", disabledText="#9AA7BE",
    danger="#D0574F", dangerBg="rgba(255,232,230,0.85)",
    input="rgba(255,255,255,0.78)", inputBorder="rgba(255,255,255,0.9)", composerBorder="rgba(255,255,255,0.95)",
    userBubble="rgba(214,228,255,0.86)", userText="#152447",
    buttonBg="rgba(255,255,255,0.60)", buttonHover="rgba(255,255,255,0.78)", buttonPressed="rgba(226,236,251,0.90)",
    scroll="rgba(120,140,175,0.28)", scrollHover="rgba(120,140,175,0.45)",
    toast="rgba(28,40,64,0.90)", overlay="rgba(20,32,58,0.30)", selection="rgba(120,165,255,0.45)",
    iconText="#5B6A85", iconTextHover="#14203A",
    iconHover="rgba(255,255,255,0.65)", iconPressed="rgba(255,255,255,0.9)",
    shadow="rgba(40,66,110,0.16)", shadowSubtle="rgba(40,66,110,0.08)", shadowFloat="rgba(40,66,110,0.26)",
    onAccent="#F7FAFF",
    menu="rgba(255,255,255,0.88)", menuBorder="rgba(255,255,255,0.95)", windowBorder="rgba(255,255,255,0.85)",
    sessionBar="rgba(255,255,255,0.42)", codeBg="rgba(238,244,255,0.85)", codeBanner="rgba(255,255,255,0.6)",
    inlineCode="rgba(226,236,251,0.9)", track="rgba(255,255,255,0.5)", ring="rgba(120,140,175,0.2)",
    toolbar="rgba(255,255,255,0.5)",
    syntax=["#B23A82", "#2C7A4B", "#7B8AA3", "#1F6FB2", "#6A4CC4", "#B25B16", "#42506B"],
)

FROST_DARK = pack_spec(
    window="#0E131C", panel="rgba(30,40,56,0.62)", card="rgba(34,45,64,0.66)",
    composer="rgba(38,50,70,0.72)", hover="rgba(56,74,104,0.55)", active="rgba(70,92,128,0.65)",
    border="rgba(160,190,235,0.16)", text1="#EAF1FC", text2="#AFC0D8", text3="#8A9CB6", text4="#6E8098",
    accent="#6FA8FF", accentHover="#8CBBFF",
    primaryBg="#5B8FF0", primaryHover="#6FA0F5", primaryPressed="#4A7BD6", primaryText="#0B1424",
    disabledBg="rgba(50,64,86,0.55)", disabledText="#6E8098",
    danger="#E0796E", dangerBg="rgba(90,36,34,0.65)",
    input="rgba(18,26,38,0.72)", inputBorder="rgba(160,190,235,0.20)", composerBorder="rgba(160,190,235,0.22)",
    userBubble="rgba(64,96,150,0.55)", userText="#EAF1FC",
    buttonBg="rgba(52,68,94,0.62)", buttonHover="rgba(64,82,112,0.72)", buttonPressed="rgba(76,96,130,0.78)",
    scroll="rgba(150,180,220,0.22)", scrollHover="rgba(150,180,220,0.40)",
    toast="rgba(22,30,44,0.92)", overlay="rgba(4,8,14,0.55)", selection="rgba(92,140,220,0.45)",
    iconText="#9FB2CB", iconTextHover="#EAF1FC",
    iconHover="rgba(120,150,200,0.16)", iconPressed="rgba(120,150,200,0.26)",
    shadow="rgba(0,8,20,0.55)", shadowSubtle="rgba(0,8,20,0.32)", shadowFloat="rgba(0,8,20,0.68)",
    onAccent="#0B1424",
    menu="rgba(28,38,54,0.94)", menuBorder="rgba(160,190,235,0.18)", windowBorder="rgba(160,190,235,0.18)",
    sessionBar="rgba(22,30,44,0.55)", codeBg="rgba(14,20,30,0.75)", codeBanner="rgba(26,36,52,0.8)",
    inlineCode="rgba(40,54,76,0.8)", track="rgba(160,190,235,0.12)", ring="rgba(160,190,235,0.20)",
    toolbar="rgba(30,40,56,0.6)",
    syntax=["#F58BC0", "#7FDDA2", "#8194AE", "#71C1F0", "#B79DFF", "#F0A868", "#B6C6DC"],
)

# 3) 荧光屏 phosphor：纯黑 CRT + 荧光绿 + 等宽字 + 直角 + 扫描线。
PHOSPHOR_DARK = pack_spec(
    window="#070A08", panel="#0B0F0C", card="#0E1410", composer="#0E1410",
    hover="#121A14", active="#17211A", border="rgba(70,255,160,0.16)",
    text1="#C9FFDF", text2="#79C79A", text3="#569B72", text4="#3E7355",
    accent="#3DFF9E", accentHover="#7CFFBE",
    primaryBg="#3DFF9E", primaryHover="#6BFFB6", primaryPressed="#22D981", primaryText="#04120A",
    disabledBg="#121A14", disabledText="#3E7355",
    danger="#FF5E5E", dangerBg="#2A0C0C", dangerText="#FF8A8A", onDanger="#120404",
    input="#0A0F0B", inputBorder="rgba(70,255,160,0.24)", composerBorder="rgba(70,255,160,0.28)",
    userBubble="#0F1B13", userText="#C9FFDF",
    buttonBg="#121A14", buttonHover="#17211A", buttonPressed="#1D2A20", tag="#0F1B13",
    scroll="#1E3325", scrollHover="#2A4733",
    toast="#0B1A10", overlay="rgba(0,0,0,0.6)", selection="rgba(61,255,158,0.30)",
    iconText="#79C79A", iconTextHover="#C9FFDF",
    iconHover="rgba(61,255,158,0.10)", iconPressed="rgba(61,255,158,0.18)",
    shadow="rgba(0,0,0,0.75)", shadowSubtle="rgba(0,0,0,0.5)", shadowFloat="rgba(0,0,0,0.85)",
    onAccent="#04120A",
    menu="#0B120D", menuBorder="rgba(70,255,160,0.22)", windowBorder="rgba(70,255,160,0.24)",
    sessionBar="#080C09", codeBg="#080D0A", codeBanner="#0C1310", inlineCode="#0E1712",
    track="rgba(70,255,160,0.12)", ring="rgba(70,255,160,0.26)", toolbar="#0E1410",
    syntax=["#FF87D7", "#7CFF9E", "#4E8B68", "#6BDFFF", "#C6A6FF", "#FFC978", "#8FD9AE"],
)

PHOSPHOR_LIGHT = pack_spec(
    window="#F4F6F2", panel="#FFFFFF", card="#FFFFFF", composer="#FFFFFF",
    hover="#EDF2EC", active="#E2EAE0", border="rgba(16,60,38,0.16)",
    text1="#0C1F15", text2="#3D5C4A", text3="#5E7C69", text4="#7E9A87",
    accent="#1B7A4B", accentHover="#12603A",
    primaryBg="#0C1F15", primaryHover="#1C3A28", primaryPressed="#08150E", primaryText="#F1FFF7",
    disabledBg="#EDF2EC", disabledText="#8FA795",
    danger="#C2402F", dangerBg="#FBEAE6", dangerText="#A63122", onDanger="#FFFFFF",
    input="#FFFFFF", inputBorder="rgba(16,60,38,0.24)", composerBorder="rgba(16,60,38,0.26)",
    userBubble="#E4F1E7", userText="#0C1F15",
    buttonBg="#EDF2EC", buttonHover="#E2EAE0", buttonPressed="#D6E0D4", tag="#E4F1E7",
    scroll="#C9D8CC", scrollHover="#AFC4B4",
    toast="#0C1F15", overlay="rgba(12,31,21,0.35)", selection="rgba(27,122,75,0.25)",
    iconText="#3D5C4A", iconTextHover="#0C1F15",
    iconHover="rgba(16,60,38,0.07)", iconPressed="rgba(16,60,38,0.13)",
    shadow="rgba(12,31,21,0.12)", shadowSubtle="rgba(12,31,21,0.06)", shadowFloat="rgba(12,31,21,0.2)",
    onAccent="#F1FFF7",
    menu="#FFFFFF", menuBorder="rgba(16,60,38,0.18)", windowBorder="rgba(16,60,38,0.22)",
    sessionBar="#F8FAF6", codeBg="#F5F8F3", codeBanner="#EDF2EC", inlineCode="#EDF2EC",
    track="rgba(16,60,38,0.10)", ring="rgba(16,60,38,0.18)", toolbar="#EDF2EC",
    syntax=["#A6305F", "#1B7A4B", "#6F8878", "#15629B", "#5A3FA8", "#9A5A12", "#39504A"],
)

# 4) 樱岚 sakura：粉白渐变 + 花瓣 + 大圆角 + 玫红强调。
SAKURA_LIGHT = pack_spec(
    window="#FFF5F7", panel="#FFFBFD", card="#FFFFFF", composer="#FFFFFF",
    hover="#FDEEF2", active="#FBE3EA", border="rgba(190,110,135,0.16)",
    text1="#3A1E27", text2="#7A5158", text3="#96696F", text4="#B0848A",
    accent="#D2567E", accentHover="#B93F68",
    primaryBg="#C94A72", primaryHover="#B23B62", primaryPressed="#9A2F53", primaryText="#FFF7FA",
    disabledBg="#FDEEF2", disabledText="#C4A0A8",
    danger="#C4444A", dangerBg="#FCEBEC", dangerText="#AE3036", onDanger="#FFF7FA",
    input="#FFFFFF", inputBorder="rgba(190,110,135,0.22)", composerBorder="rgba(190,110,135,0.24)",
    userBubble="#FCE4EC", userText="#3A1E27",
    buttonBg="#FDEEF2", buttonHover="#FBE3EA", buttonPressed="#F7D6E0", tag="#FCE4EC",
    scroll="#F0C9D4", scrollHover="#E2AFC0",
    toast="#4A2430", overlay="rgba(58,30,39,0.30)", selection="rgba(210,86,126,0.28)",
    iconText="#8A5E66", iconTextHover="#3A1E27",
    iconHover="rgba(190,110,135,0.09)", iconPressed="rgba(190,110,135,0.16)",
    shadow="rgba(140,70,95,0.16)", shadowSubtle="rgba(140,70,95,0.08)", shadowFloat="rgba(140,70,95,0.24)",
    onAccent="#FFF7FA",
    menu="#FFFFFF", menuBorder="rgba(190,110,135,0.18)", windowBorder="rgba(190,110,135,0.22)",
    sessionBar="#FFF8FA", codeBg="#FDF6F8", codeBanner="#FBEDF2", inlineCode="#FBEDF2",
    track="rgba(190,110,135,0.12)", ring="rgba(190,110,135,0.20)", toolbar="#FDEEF2",
    syntax=["#B83A6B", "#2F7A52", "#A08590", "#2A6EA8", "#7A4BB8", "#B2653A", "#6B4A52"],
)

SAKURA_DARK = pack_spec(
    window="#1B1216", panel="#241819", card="#2A1C1F", composer="#2A1C1F",
    hover="#312023", active="#3A272B", border="rgba(255,200,215,0.10)",
    text1="#FBECF1", text2="#D3B4BC", text3="#AB8B94", text4="#8A6C75",
    accent="#F58AAE", accentHover="#FFA8C3",
    primaryBg="#F07A9E", primaryHover="#FF90B0", primaryPressed="#D95F86", primaryText="#2A0E17",
    disabledBg="#33262A", disabledText="#7C5C66",
    danger="#F0736F", dangerBg="#3B1E1E", dangerText="#F59A96", onDanger="#2A0E17",
    input="#211618", inputBorder="rgba(255,200,215,0.16)", composerBorder="rgba(255,200,215,0.18)",
    userBubble="#3B242A", userText="#FBECF1",
    buttonBg="#312023", buttonHover="#3A272B", buttonPressed="#442F34", tag="#3B242A",
    scroll="#4E363C", scrollHover="#61454C",
    toast="#3B242A", overlay="rgba(10,4,7,0.55)", selection="rgba(245,138,174,0.32)",
    iconText="#C6A3AC", iconTextHover="#FBECF1",
    iconHover="rgba(255,200,215,0.10)", iconPressed="rgba(255,200,215,0.18)",
    shadow="rgba(0,0,0,0.5)", shadowSubtle="rgba(0,0,0,0.3)", shadowFloat="rgba(0,0,0,0.62)",
    onAccent="#2A0E17",
    menu="#312023", menuBorder="rgba(255,200,215,0.14)", windowBorder="rgba(255,200,215,0.16)",
    sessionBar="#241819", codeBg="#1F1417", codeBanner="#271A1D", inlineCode="#271A1D",
    track="rgba(255,200,215,0.10)", ring="rgba(255,200,215,0.18)", toolbar="#312023",
    syntax=["#FF9EC2", "#8FE0A8", "#A98D96", "#7FC8F0", "#C6A0F0", "#F0B078", "#D0AEB6"],
)

# 5) 苏黎世 swiss：纯白纸 + 纯黑描边 + 一点红，直角，无阴影（版面感）。
SWISS_LIGHT = pack_spec(
    window="#FFFFFF", panel="#FFFFFF", card="#FFFFFF", composer="#FFFFFF",
    hover="#F2F2F2", active="#E8E8E8", border="#111111",
    text1="#0A0A0A", text2="#444444", text3="#666666", text4="#8A8A8A",
    accent="#E03131", accentHover="#C42B2B",
    primaryBg="#111111", primaryHover="#000000", primaryPressed="#333333", primaryText="#FFFFFF",
    disabledBg="#F2F2F2", disabledText="#AAAAAA",
    danger="#E03131", dangerBg="#FDECEC", dangerText="#C42B2B", onDanger="#FFFFFF",
    input="#FFFFFF", inputBorder="#111111", composerBorder="#111111",
    userBubble="#F2F2F2", userText="#0A0A0A",
    buttonBg="#FFFFFF", buttonHover="#F2F2F2", buttonPressed="#E8E8E8", tag="#F2F2F2",
    scroll="#CCCCCC", scrollHover="#111111",
    toast="#111111", overlay="rgba(0,0,0,0.35)", selection="#FFD8D8",
    iconText="#444444", iconTextHover="#0A0A0A",
    iconHover="#F2F2F2", iconPressed="#E8E8E8",
    shadow="rgba(0,0,0,0)", shadowSubtle="rgba(0,0,0,0)", shadowFloat="rgba(0,0,0,0.10)",
    onAccent="#FFFFFF",
    menu="#FFFFFF", menuBorder="#111111", windowBorder="#111111",
    sessionBar="#FAFAFA", codeBg="#F7F7F7", codeBanner="#EFEFEF", inlineCode="#F2F2F2",
    track="rgba(0,0,0,0.10)", ring="#111111", toolbar="#F2F2F2",
    syntax=["#E03131", "#2B8A3E", "#868E96", "#1971C2", "#7048E8", "#E8590C", "#495057"],
)

SWISS_DARK = pack_spec(
    window="#0C0C0C", panel="#141414", card="#171717", composer="#171717",
    hover="#212121", active="#2A2A2A", border="#3D3D3D",
    text1="#F5F5F5", text2="#B8B8B8", text3="#909090", text4="#6E6E6E",
    accent="#FF4D4D", accentHover="#FF6B6B",
    primaryBg="#F5F5F5", primaryHover="#FFFFFF", primaryPressed="#D6D6D6", primaryText="#0C0C0C",
    disabledBg="#1E1E1E", disabledText="#5E5E5E",
    danger="#FF4D4D", dangerBg="#2E1414", dangerText="#FF7A7A", onDanger="#0C0C0C",
    input="#0C0C0C", inputBorder="#3D3D3D", composerBorder="#3D3D3D",
    userBubble="#212121", userText="#F5F5F5",
    buttonBg="#141414", buttonHover="#212121", buttonPressed="#2A2A2A", tag="#212121",
    scroll="#333333", scrollHover="#F5F5F5",
    toast="#F5F5F5", overlay="rgba(0,0,0,0.6)", selection="rgba(255,77,77,0.30)",
    iconText="#B8B8B8", iconTextHover="#F5F5F5",
    iconHover="#212121", iconPressed="#2A2A2A",
    shadow="rgba(0,0,0,0)", shadowSubtle="rgba(0,0,0,0)", shadowFloat="rgba(0,0,0,0.6)",
    onAccent="#0C0C0C",
    menu="#171717", menuBorder="#3D3D3D", windowBorder="#3D3D3D",
    sessionBar="#0F0F0F", codeBg="#111111", codeBanner="#171717", inlineCode="#1E1E1E",
    track="rgba(255,255,255,0.12)", ring="#3D3D3D", toolbar="#171717",
    syntax=["#FF6B6B", "#69DB7C", "#8A8A8A", "#4DABF7", "#B197FC", "#FFA94D", "#CED4DA"],
)

# 6) iOS 玻璃 ios：iOS 系统色 + 半透明材质 + 连续大圆角，毛玻璃压在极光壁纸上。
#    取色按 Apple 公开的系统色（light: #007AFF/#FF3B30/#F2F2F7；dark: #0A84FF/#FF453A/#1C1C1E），
#    文字层级用 secondaryLabel 的 alpha（0.6 / 0.4）而不是另选灰，这是 iOS 的语义色做法。
IOS_LIGHT = pack_spec(
    # 底色是中性冷灰白：不用纯白，纯白会让上面的白玻璃完全消失，留一档灰才分得出层
    window="#E9ECF2", panel="rgba(255,255,255,0.90)", card="rgba(255,255,255,0.95)",
    composer="rgba(255,255,255,0.58)", hover="rgba(15,23,42,0.05)", active="rgba(15,23,42,0.085)",
    # 描边压到 0.07：现代界面靠明度差分层，不靠描边画框
    border="rgba(15,23,42,0.07)", text1="#12131A", text2="rgba(18,19,26,0.64)",
    text3="rgba(18,19,26,0.45)", text4="rgba(18,19,26,0.30)",
    accent="#007AFF", accentHover="#0066DB",
    primaryBg="#007AFF", primaryHover="#1A88FF", primaryPressed="#0063D1", primaryText="#FFFFFF",
    disabledBg="rgba(15,23,42,0.055)", disabledText="rgba(18,19,26,0.28)",
    danger="#FF3B30", dangerBg="rgba(255,59,48,0.10)", dangerText="#D70015", onDanger="#FFFFFF",
    input="rgba(15,23,42,0.045)", inputBorder="rgba(15,23,42,0.08)", composerBorder="rgba(15,23,42,0.07)",
    userBubble="#0A7AFF", userText="#FFFFFF",
    # 次要按钮是极淡灰填充，不是白玻璃 —— 白玻璃在淡色主题里等于没有底
    buttonBg="rgba(15,23,42,0.045)", buttonHover="rgba(15,23,42,0.072)", buttonPressed="rgba(15,23,42,0.10)",
    tag="rgba(15,23,42,0.055)",
    scroll="rgba(15,23,42,0.18)", scrollHover="rgba(15,23,42,0.32)",
    toast="rgba(28,29,34,0.92)", overlay="rgba(16,18,24,0.24)", selection="rgba(0,122,255,0.20)",
    iconText="rgba(18,19,26,0.55)", iconTextHover="#12131A",
    iconHover="rgba(15,23,42,0.06)", iconPressed="rgba(15,23,42,0.10)",
    shadow="rgba(15,23,42,0.13)", shadowSubtle="rgba(15,23,42,0.055)", shadowFloat="rgba(15,23,42,0.16)",
    onAccent="#FFFFFF",
    menu="rgba(255,255,255,0.92)", menuBorder="rgba(15,23,42,0.08)", windowBorder="rgba(15,23,42,0.09)",
    sessionBar="rgba(255,255,255,0.66)", codeBg="rgba(15,23,42,0.035)", codeBanner="rgba(15,23,42,0.022)",
    inlineCode="rgba(15,23,42,0.05)", track="rgba(15,23,42,0.05)", ring="rgba(15,23,42,0.10)",
    toolbar="rgba(255,255,255,0.72)",
    syntax=["#9A3AA0", "#B8382E", "#7C7C85", "#205C86", "#4B34A8", "#96521C", "#6E6E77"],
)

IOS_DARK = pack_spec(
    window="#0B0B0E", panel="rgba(31,31,36,0.88)", card="rgba(38,38,44,0.93)",
    composer="rgba(34,34,39,0.62)", hover="rgba(255,255,255,0.055)", active="rgba(255,255,255,0.09)",
    border="rgba(255,255,255,0.09)", text1="#F7F7F9", text2="rgba(242,242,247,0.66)",
    text3="rgba(242,242,247,0.45)", text4="rgba(242,242,247,0.28)",
    accent="#0A84FF", accentHover="#3D9BFF",
    primaryBg="#0A84FF", primaryHover="#3396FF", primaryPressed="#0069DB", primaryText="#FFFFFF",
    disabledBg="rgba(255,255,255,0.07)", disabledText="rgba(242,242,247,0.26)",
    danger="#FF453A", dangerBg="rgba(255,69,58,0.16)", dangerText="#FF6961", onDanger="#FFFFFF",
    input="rgba(255,255,255,0.06)", inputBorder="rgba(255,255,255,0.10)", composerBorder="rgba(255,255,255,0.08)",
    userBubble="#0A84FF", userText="#FFFFFF",
    buttonBg="rgba(255,255,255,0.07)", buttonHover="rgba(255,255,255,0.105)", buttonPressed="rgba(255,255,255,0.145)",
    tag="rgba(255,255,255,0.075)",
    scroll="rgba(255,255,255,0.18)", scrollHover="rgba(255,255,255,0.34)",
    toast="rgba(58,58,62,0.94)", overlay="rgba(0,0,0,0.50)", selection="rgba(10,132,255,0.32)",
    iconText="rgba(242,242,247,0.60)", iconTextHover="#FFFFFF",
    iconHover="rgba(255,255,255,0.075)", iconPressed="rgba(255,255,255,0.12)",
    shadow="rgba(0,0,0,0.55)", shadowSubtle="rgba(0,0,0,0.30)", shadowFloat="rgba(0,0,0,0.62)",
    onAccent="#FFFFFF",
    menu="rgba(30,30,34,0.94)", menuBorder="rgba(255,255,255,0.10)", windowBorder="rgba(255,255,255,0.10)",
    sessionBar="rgba(28,28,33,0.68)", codeBg="rgba(255,255,255,0.045)", codeBanner="rgba(255,255,255,0.025)",
    inlineCode="rgba(255,255,255,0.07)", track="rgba(255,255,255,0.055)", ring="rgba(255,255,255,0.12)",
    toolbar="rgba(24,24,28,0.74)",
    syntax=["#E17BD0", "#EF7A70", "#8E8E96", "#5AC8FA", "#C0A6FF", "#E8A05C", "#D6D6DC"],
)

PACKS = [
    ("papercraft", "素纸 Papercraft", "宣纸底 + 细网格 + 硬投影，印刷品的手感", PAPER_LIGHT, PAPER_DARK),
    ("frost", "霜玻 Frost", "冷蓝光斑 + 半透明玻璃面，大圆角与柔光", FROST_LIGHT, FROST_DARK),
    ("phosphor", "荧光屏 Phosphor", "CRT 终端：纯黑、荧光绿、等宽字与扫描线", PHOSPHOR_LIGHT, PHOSPHOR_DARK),
    ("sakura", "樱岚 Sakura", "樱花粉渐变 + 花瓣点缀，柔软的大圆角", SAKURA_LIGHT, SAKURA_DARK),
    ("swiss", "苏黎世 Swiss", "纯黑白 + 一点红，直角粗描边，版面化", SWISS_LIGHT, SWISS_DARK),
    ("ios", "iOS 玻璃 iOS Glass", "极光壁纸 + 毛玻璃材质 + 系统蓝与连续圆角", IOS_LIGHT, IOS_DARK),
]


def build(spec, mode, meta):
    template_path = os.path.join(BUILTIN, "theme-%s.json" % mode)
    template = json.load(open(template_path, encoding="utf-8"))
    # ⚠️ 色板里不能放 _source / _syntaxNote 这类说明键：ThemeBridge 会把每个键都当颜色解析，
    # 解析失败会刷 "调色板里的颜色解析失败" 警告。说明写在包 README 里。
    out = {}
    mapping = dict(KEYMAP)
    for key, _unused in KEYMAP:
        out[key] = spec[mapping[key]]
    for key, value in zip(SYNTAX_KEYS, spec["syntax"]):
        out[key] = value
    # 以模板键序输出，未知的新键（内置升级后加的）用模板原值兜底，保证永不断档
    ordered = {}
    for key in template:
        if key.startswith("_"):
            continue
        ordered[key] = out.get(key, template.get(key))
    for key, value in out.items():
        if key not in ordered and not key.startswith("_"):
            ordered[key] = value
    return ordered


def main():
    for slug, title, blurb, light, dark in PACKS:
        target = os.path.join(ROOT, slug)
        os.makedirs(target, exist_ok=True)
        for mode, spec in (("light", light), ("dark", dark)):
            meta = "%s · %s（%s 模式）—— 由 assets/style-packs/_build/gen_palettes.py 展开，76 键完整。" % (
                title, blurb, mode)
            data = build(spec, mode, meta)
            path = os.path.join(target, "theme-%s.json" % mode)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
                f.write("\n")
            print("wrote", os.path.relpath(path, ROOT), len(data), "keys")


if __name__ == "__main__":
    main()
