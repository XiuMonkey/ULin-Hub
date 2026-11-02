// 渲染试验用 stub：模拟宿主单例 WindowBridge。
pragma Singleton
import QtQuick

QtObject {
    property bool maximized: false
    property bool fullScreen: false
    property int contentInset: 0

    function attachWindow(w) { }
    function addWindowControl(item, kind) { }
    function removeWindowControl(item) { }
    function setCaptionItem(item) { }
    function minimize() { }
    function toggleMaximize() { }
    function closeWindow() { }
}
