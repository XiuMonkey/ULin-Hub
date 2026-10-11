// 系统提示（居中小灰字），对应旧壳 SystemMessageUnit。
// ⚠️ 不要再声明 `property string text`：根类型 Text 自带 text，重名会让整个组件加载失败
//    （报 "Type SystemNotice unavailable"，外面看到的就是消息区空白）。

import QtQuick
import ULinHub 1.0

Text {
    id: root

    textFormat: Text.MarkdownText
    wrapMode: Text.Wrap
    horizontalAlignment: Text.AlignHCenter
    color: Theme.color.systemMessageText
    font.family: "Microsoft YaHei UI"
    font.pixelSize: Theme.font.caption
}
