// 渲染试验用 stub：模拟宿主单例 Hub（会话/附件等）。
pragma Singleton
import QtQuick

QtObject {
    property int attachmentRevision: 0

    function attachmentSource(id) { return "" }
}
