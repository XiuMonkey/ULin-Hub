// 系统提示（居中小灰字），对应旧壳 SystemMessageUnit。
// ⚠️ 不要再声明 `property string text`：根类型自带 text，重名会让整个组件加载失败
//    （报 "Type SystemNotice unavailable"，外面看到的就是消息区空白）。
// 根类型 Text → TextEdit（只读可选）：为了能选中/复制提示文字；高度显式给 contentHeight，
// 调用方（MessageListView 的行高）也读 contentHeight —— TextEdit 在宽度定下来前没有可用的 implicit*。

import QtQuick
import ULinHub 1.0

TextEdit {
    id: root

    objectName: "bubbleText"
    textFormat: TextEdit.MarkdownText
    wrapMode: TextEdit.Wrap
    horizontalAlignment: TextEdit.AlignHCenter
    readOnly: true
    selectByMouse: true
    persistentSelection: true
    activeFocusOnPress: false
    height: contentHeight
    color: Theme.color.systemMessageText
    selectionColor: Theme.color.selectionBg
    selectedTextColor: Theme.color.systemMessageText
    font.family: "Microsoft YaHei UI"
    font.pixelSize: Theme.font.caption
}
