// 由 gen_theme.py 生成，勿手改。
pragma Singleton
import QtQuick

QtObject {
    readonly property bool isDark: true

    readonly property QtObject color: QtObject {
        readonly property color windowBg: "#0B0B0E"
        readonly property color panelBg: "#E01F1F24"
        readonly property color composerBg: "#9E222227"
        readonly property color cardBg: "#ED26262C"
        readonly property color hoverBg: "#0EFFFFFF"
        readonly property color activeBg: "#17FFFFFF"
        readonly property color border: "#17FFFFFF"
        readonly property color textPrimary: "#F7F7F9"
        readonly property color textSecondary: "#A8F2F2F7"
        readonly property color accent: "#0A84FF"
        readonly property color accentHover: "#3D9BFF"
        readonly property color primaryButtonBg: "#0A84FF"
        readonly property color primaryButtonHover: "#3396FF"
        readonly property color primaryButtonPressed: "#0069DB"
        readonly property color primaryButtonText: "#FFFFFF"
        readonly property color primaryButtonDisabledBg: "#12FFFFFF"
        readonly property color primaryButtonDisabledText: "#42F2F2F7"
        readonly property color danger: "#FF453A"
        readonly property color dangerBg: "#29FF453A"
        readonly property color dangerButtonBg: "#FF453A"
        readonly property color dangerButtonText: "#FFFFFF"
        readonly property color inputBg: "#0FFFFFFF"
        readonly property color userBubbleBg: "#0A84FF"
        readonly property color userBubbleText: "#FFFFFF"
        readonly property color scrollbar: "#2EFFFFFF"
        readonly property color scrollbarHover: "#57FFFFFF"
        readonly property color inputBorder: "#1AFFFFFF"
        readonly property color buttonBg: "#12FFFFFF"
        readonly property color buttonHover: "#1BFFFFFF"
        readonly property color buttonPressed: "#25FFFFFF"
        readonly property color scrollbarHandle: "#2EFFFFFF"
        readonly property color scrollbarHandleHover: "#57FFFFFF"
        readonly property color sendButtonHover: "#3396FF"
        readonly property color sendButtonPressed: "#0069DB"
        readonly property color toastBg: "#F03A3A3E"
        readonly property color overlayBg: "#80000000"
        readonly property color tagBg: "#13FFFFFF"
        readonly property color selectionBg: "#520A84FF"
        readonly property color iconButtonText: "#99F2F2F7"
        readonly property color iconButtonTextHover: "#FFFFFF"
        readonly property color iconButtonHoverBg: "#13FFFFFF"
        readonly property color iconButtonPressedBg: "#1FFFFFFF"
        readonly property color loadingText: "#A8F2F2F7"
        readonly property color loadingBorder: "#1AFFFFFF"
        readonly property color systemMessageText: "#73F2F2F7"
        readonly property color shadow: "#61000000"
        readonly property color shadowSubtle: "#38000000"
        readonly property color shadowFloat: "#75000000"
        readonly property color textOnAccent: "#FFFFFF"
        readonly property color composerBorder: "#14FFFFFF"
        readonly property color textTertiary: "#73F2F2F7"
        readonly property color textCaption: "#47F2F2F7"
        readonly property color chipHoverBg: "#13FFFFFF"
        readonly property color menuBg: "#F01E1E22"
        readonly property color menuBorder: "#1AFFFFFF"
        readonly property color windowBorder: "#1AFFFFFF"
        readonly property color captionHoverBg: "#17FFFFFF"
        readonly property color sessionBarBg: "#AD1C1C21"
        readonly property color codeBlockBg: "#0BFFFFFF"
        readonly property color codeBlockBannerBg: "#06FFFFFF"
        readonly property color inlineCodeBg: "#12FFFFFF"
        readonly property color trackBg: "#0EFFFFFF"
        readonly property color indicatorBg: "#ED26262C"
        readonly property color ringBg: "#1FFFFFFF"
        readonly property color dangerText: "#FF6961"
        readonly property color linkText: "#3D9BFF"
        readonly property color toolbarBg: "#BD18181C"
        readonly property color syntaxKeyword: "#E17BD0"
        readonly property color syntaxString: "#EF7A70"
        readonly property color syntaxComment: "#8E8E96"
        readonly property color syntaxConstant: "#5AC8FA"
        readonly property color syntaxFunction: "#C0A6FF"
        readonly property color syntaxVariable: "#E8A05C"
        readonly property color syntaxPunct: "#D6D6DC"
    }

    readonly property QtObject radius: QtObject {
        readonly property real window: 14
        readonly property real panel: 14
        readonly property real card: 14
        readonly property real inner: 12
        readonly property real control: 10
        readonly property real input: 10
        readonly property real small: 8
        readonly property real check: 6
        readonly property real tiny: 5
        readonly property real dot: 9
        readonly property real chip: 12
        readonly property real pill: 14
        readonly property real popup: 14
        readonly property real menu: 14
        readonly property real float: 18
        readonly property real composer: 18
    }

    readonly property QtObject font: QtObject {
        readonly property real title: 15
        readonly property real body: 13
        readonly property real caption: 12
        readonly property real small: 11
        readonly property real glyph: 12
        readonly property real lead: 16
        readonly property real display: 20
        readonly property real weightBold: 600
        readonly property real weightMedium: 500
    }

    readonly property QtObject spacing: QtObject {
        readonly property real windowMargin: 24
        readonly property real columnClearance: 16
        readonly property real panelGap: 2
        readonly property real contentSideInset: 9
        readonly property real cardPadding: 16
        readonly property real sectionGap: 14
        readonly property real buttonGap: 8
        readonly property real buttonPadY: 9
        readonly property real buttonPadX: 16
        readonly property real fieldPadY: 8
        readonly property real fieldPadX: 10
        readonly property real fieldPadWideY: 10
        readonly property real fieldPadWideX: 12
        readonly property real labelGap: 8
        readonly property real editorPadTop: 4
        readonly property real statsGap: 4
    }

    readonly property QtObject size: QtObject {
        readonly property real sessionBarWidth: 240
        readonly property real panelBarWidth: 300
        readonly property real panelBarMinWidth: 260
        readonly property real titleBarHeight: 46
        readonly property real titleBarPadLeft: 16
        readonly property real windowButtonWidth: 30
        readonly property real windowButtonHeight: 30
        readonly property real windowButtonTopMargin: 10
        readonly property real windowButtonRightMargin: 12
        readonly property real logoHeight: 18
        readonly property real logoTextGap: 8
        readonly property real statsLineHeight: 14
        readonly property real minWindowWidth: 720
        readonly property real minWindowHeight: 480
        readonly property real preferredWindowWidth: 1440
        readonly property real preferredWindowHeight: 880
        readonly property real minConversationWidth: 320
        readonly property real sendButton: 32
        readonly property real selectorChipHeight: 24
        readonly property real scrollbarThickness: 12
        readonly property real scrollbarHandleWidth: 6
        readonly property real scrollbarHandleMin: 30
        readonly property real editorMinHeight: 48
        readonly property real editorMaxHeight: 140
        readonly property real dropdownArrowWidth: 18
        readonly property real comboMinWidth: 200
        readonly property real checkbox: 18
        readonly property real checkboxCompact: 16
        readonly property real radioIndicator: 18
        readonly property real listItemMinHeight: 36
        readonly property real entryHeaderMinHeight: 48
        readonly property real directoryHeaderMinHeight: 44
        readonly property real switchTrackWidth: 34
        readonly property real switchTrackHeight: 18
        readonly property real initCardWidth: 400
        readonly property real initCardHeight: 240
        readonly property real tooltipMaxWidth: 360
        readonly property real menuMinWidth: 300
        readonly property real menuFallbackWidth: 220
    }

    readonly property QtObject shadow: QtObject {
        readonly property real cardBlur: 18
        readonly property real cardDy: 2
        readonly property real cardRadius: 16
        readonly property real inputBlur: 18
        readonly property real inputDy: 4
        readonly property real inputRadius: 24
        readonly property real floatBlur: 26
        readonly property real floatDy: 5
        readonly property real floatRadius: 20
        readonly property real topBarGap: 8
    }

    readonly property QtObject motion: QtObject {
        readonly property real fast: 160
        readonly property real normal: 260
        readonly property real slow: 420
    }
}
