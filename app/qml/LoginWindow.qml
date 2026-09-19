import QtQuick
import FluentUI
import bbhouse

FluWindow {
    id: window
    width: 640
    height: 780
    minimumWidth: 520
    minimumHeight: 600
    launchMode: FluWindowType.SingleTask
    title: qsTr("登录")
    Component.onDestruction: LoginController.cancel()
    LoginPage {
        anchors.fill: parent
        onCompleted: {
            FluRouter.navigate("/")
            window.close()
        }
    }
}
