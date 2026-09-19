import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FluentUI

FluPage {
    id: page
    padding: 0
    signal completed()
    property bool showCookieHelp: false

    Component.onDestruction: LoginController.cancel()
    Connections {
        target: LoginController
        function onAuthenticated() { cookie_input.text = ""; page.completed() }
    }
    FileDialog {
        id: cookie_file
        title: qsTr("导入 Cookie 文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("文本文件 (*.txt)"), qsTr("所有文件 (*)")]
        onAccepted: LoginController.importFile(selectedFile)
    }
    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentWidth: width
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FluScrollBar {}
        ColumnLayout {
            id: content
            width: parent.width
            spacing: 16
            FluText { text: qsTr("登录哔哩哔哩"); font: FluTextStyle.Title }
            FluText {
                Layout.fillWidth: true
                text: qsTr("导入 Cookie 后即可使用在线功能，也可以稍后登录并使用本地媒体库。")
                wrapMode: Text.Wrap
                textColor: FluTheme.fontSecondaryColor
            }
            FluText { text: qsTr("导入 Cookie"); font: FluTextStyle.Subtitle }
            FluText {
                Layout.fillWidth: true
                text: qsTr("可选择 bilibili.cookie.txt，或粘贴浏览器请求中的 Cookie（需包含 SESSDATA）。")
                wrapMode: Text.Wrap
                textColor: FluTheme.fontSecondaryColor
            }
            FluTextButton {
                text: page.showCookieHelp ? qsTr("收起导入帮助") : qsTr("如何获取 Cookie？")
                onClicked: page.showCookieHelp = !page.showCookieHelp
            }
            ColumnLayout {
                visible: page.showCookieHelp
                Layout.fillWidth: true
                spacing: 10
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("1. 在 Microsoft Edge 安装 Cookie-Editor 扩展。")
                    wrapMode: Text.Wrap
                }
                FluTextButton {
                    text: qsTr("打开 Cookie-Editor 扩展商店")
                    onClicked: Qt.openUrlExternally("https://microsoftedge.microsoft.com/addons/detail/cookieeditor/neaplmfkghagebokkhpjpoebhdledlfi")
                }
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("2. 在同一浏览器打开 https://www.bilibili.com 并登录，保持该标签页为当前页面。")
                    wrapMode: Text.Wrap
                }
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("3. 打开 Cookie-Editor，点击 Export（导出），选择 Header String（Cookie 请求头字符串）。导出内容会复制到剪贴板，请勿选择 JSON 或 Netscape。")
                    wrapMode: Text.Wrap
                }
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("4. 将完整内容粘贴到下方，点击“验证并导入”；也可保存为 UTF-8 纯文本文件后选择导入。内容需包含 SESSDATA，成功后会自动保存为 bilibili.cookie.txt。")
                    wrapMode: Text.Wrap
                }
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("Cookie 是账号登录凭据，请勿分享或上传。若提示无效或过期，请在网页重新登录后再次导出。")
                    wrapMode: Text.Wrap
                    textColor: FluTheme.fontSecondaryColor
                }
            }
            FluPasswordBox {
                id: cookie_input
                objectName: "loginCookieInput"
                Layout.fillWidth: true
                placeholderText: qsTr("粘贴 Cookie")
                enabled: !LoginController.busy
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                FluButton {
                    text: qsTr("选择 Cookie 文件")
                    disabled: LoginController.busy
                    onClicked: cookie_file.open()
                }
                FluFilledButton {
                    objectName: "loginImportButton"
                    text: qsTr("验证并导入")
                    disabled: LoginController.busy || cookie_input.text.trim() === ""
                    onClicked: {
                        var value = cookie_input.text
                        cookie_input.text = ""
                        LoginController.importText(value)
                    }
                }
            }
            FluText {
                Layout.fillWidth: true
                text: qsTr("凭据保存在：%1").arg(LoginController.cookiePath)
                wrapMode: Text.WrapAnywhere
                textColor: FluTheme.fontSecondaryColor
                font.pixelSize: 12
            }
            RowLayout {
                visible: LoginController.busy || LoginController.status !== ""
                Layout.fillWidth: true
                FluProgressRing { visible: LoginController.busy; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                FluText { text: LoginController.status; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            FluText {
                Layout.fillWidth: true
                visible: LoginController.error !== ""
                text: LoginController.error
                textColor: FluTheme.dark ? "#ffb4ab" : "#ba1a1a"
                wrapMode: Text.Wrap
            }
            FluButton {
                objectName: "loginSkipButton"
                text: qsTr("稍后登录，继续使用")
                onClicked: { cookie_input.text = ""; LoginController.cancel(); page.completed() }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }
}
