// 渲染试验用 stub：模拟宿主单例 Style（真实环境由 StyleResolver 提供）。
// 中间产物在 _work/ui，所以这里指过去。
pragma Singleton
import QtQuick

QtObject {
    function url(logicalPath) {
        return Qt.resolvedUrl("../_work/ui/" + logicalPath)
    }
}
