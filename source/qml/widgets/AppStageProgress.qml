// 阶段进度条：默认 4 段，每段对应一个真实阶段（解析依赖 → 下载 → 链接依赖 → 构建）。
// 完成段填满、当前段呼吸、未来段留空：不编造百分比，但能一眼看出卡在哪一步。

import QtQuick
import ULinHub 1.0

Item {
    id: root

    property int stage: 0      // 0 = 未知（不画）；1..count = 当前阶段
    property int count: 4
    property real barHeight: 5

    implicitHeight: barHeight
    height: barHeight

    Row {
        anchors.fill: parent
        spacing: 4

        Repeater {
            model: root.count

            Rectangle {
                required property int index

                width: Math.max(0, (root.width - (root.count - 1) * 4) / root.count)   // 先扣掉段间缝（= Row.spacing）再等分
                height: root.height
                radius: height / 2

                readonly property bool doneStage: index < root.stage - 1
                readonly property bool currentStage: index === root.stage - 1

                color: doneStage || currentStage ? Theme.color.accent : Theme.color.inputBg
                opacity: currentStage ? 0.5 : 1

                SequentialAnimation on opacity {
                    running: currentStage && root.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 1.0; duration: 500; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.45; duration: 500; easing.type: Easing.InOutSine }
                }
            }
        }
    }
}
