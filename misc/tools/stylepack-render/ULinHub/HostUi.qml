// 渲染试验用 stub：模拟宿主单例 HostUi（扩展注入点注册表）。
pragma Singleton
import QtQuick

QtObject {
    function registerItem(name, item) { }
    function item(name) { return null }
}
