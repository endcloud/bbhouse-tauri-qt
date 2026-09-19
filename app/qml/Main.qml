import QtQuick
import FluentUI

FluLauncher {
    id: launcher

    Connections {
        target: LoginController
        function onLoginRequested() { FluRouter.navigate("/login") }
    }

    Component.onCompleted: {
        FluApp.windowIcon = Qt.platform.os === "osx"
                ? "qrc:/icons/bbhouse-icon-1024-mac.png"
                : "qrc:/icons/bbhouse-icon-1024.png"
        FluApp.init(launcher)
        // macOS:启用系统原生标题栏(红绿灯不与应用内汉堡/搜索框冲突,
        // 自带圆角与原生窗口感;Windows 保持沉浸式自绘标题栏)
        if (FluTools.isMacos()) FluApp.useSystemAppBar = true
        FluRouter.routes = {
            "/": "qrc:/qt/qml/bbhouse/qml/MainWindow.qml",
            "/login": "qrc:/qt/qml/bbhouse/qml/LoginWindow.qml",
            "/player": "qrc:/qt/qml/bbhouse/qml/PlayerWindow.qml",
            "/live-player": "qrc:/qt/qml/bbhouse/qml/LivePlayerWindow.qml",
            "/history-service": "qrc:/qt/qml/bbhouse/qml/HistoryServiceWindow.qml"
        }
        FluTheme.darkMode = Qt.binding(function () {
            return AppController.theme === "dark" ? 2 : AppController.theme === "light" ? 1 : 0
        })
        FluRouter.navigate(LoginController.needsLogin ? "/login" : "/")
        // 冒烟导航:NAV 列表含 PlayerWindow.qml 时直接导航打开播放窗口
        // (player 窗口仅交互后打开,冒烟只验证 QML 可装载)
        var nav = (typeof bbhouseSmokeNav !== "undefined" && bbhouseSmokeNav) ? String(bbhouseSmokeNav) : ""
        if (nav.split(",").indexOf("PlayerWindow.qml") !== -1) {
            FluRouter.navigate("/player")
        }
        if (nav.split(",").indexOf("LivePlayerWindow.qml") !== -1) {
            FluRouter.navigate("/live-player")
        }
        if (nav.split(",").indexOf("HistoryServiceWindow.qml") !== -1) {
            FluRouter.navigate("/history-service")
        }
        // 冒烟起播:给定 aid 构造 archive 条目走真实播放解析链(需 cookie)
        var playAid = (typeof bbhouseSmokePlayAid !== "undefined" && bbhouseSmokePlayAid) ? String(bbhouseSmokePlayAid) : ""
        if (playAid !== "") {
            PlayerController.openWith([{
                videoKey: "av:" + playAid,
                title: "smoke-" + playAid,
                subtitle: "",
                coverUrl: "",
                business: "archive",
                oid: parseInt(playAid),
                kid: 0,
                duration: 0,
                linkUrl: ""
            }])
            FluRouter.navigate("/player")
        }
    }
}
