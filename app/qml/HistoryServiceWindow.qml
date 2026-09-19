import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

FluWindow {
    id: window
    width: 760
    height: 720
    minimumWidth: 600
    minimumHeight: 600
    title: qsTr("本地历史 · 定时服务")
    launchMode: FluWindowType.SingleTask
    property bool draftLoaded: false

    function restoreDraft() {
        timeInput.text = HistoryServiceController.time
        cycleInput.currentIndex = HistoryServiceController.cycle === "weekly" ? 1 : 0
        weekdayInput.currentIndex = HistoryServiceController.weekday - 1
        draftLoaded = true
    }
    function statusText(value) {
        if (value === "success") return qsTr("成功")
        if (value === "failed") return qsTr("失败")
        if (value === "cancelled") return qsTr("已取消")
        return qsTr("进行中")
    }
    function displayTime(value) {
        if (!value) return "—"
        var date = new Date(value)
        return isNaN(date.getTime()) ? value : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss")
    }

    Item {
        id: root
        anchors.fill: parent
        FluInfoBar { id: info; root: root }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                FluText { text: qsTr("定时服务"); font: FluTextStyle.Title; Layout.fillWidth: true }
                FluProgressRing { visible: HistoryServiceController.busy; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                FluIconButton {
                    iconSource: FluentIcons.Refresh
                    disabled: HistoryServiceController.busy
                    Accessible.name: qsTr("刷新服务状态与日志")
                    onClicked: HistoryServiceController.refresh()
                    FluTooltip { text: qsTr("刷新服务状态与日志"); visible: parent.hovered; delay: 500 }
                }
            }
            FluText {
                Layout.fillWidth: true
                text: !HistoryServiceController.statusKnown ? qsTr("状态未知") :
                    !HistoryServiceController.registered ? qsTr("未注册") :
                    HistoryServiceController.enabled ? qsTr("已注册 · 已启用") : qsTr("已注册 · 已暂停")
                font: FluTextStyle.BodyStrong
            }
            FluText {
                Layout.fillWidth: true
                text: FluTools.isMacos()
                    ? qsTr("使用当前用户的系统定时任务。保持登录后，关闭主窗口也可运行；休眠期间错过的计划可能在唤醒后执行一次。")
                    : qsTr("使用 Windows Service（LocalService）。关闭应用或退出登录后仍可运行；注册、修改、启停和卸载需 UAC 授权。关机或休眠错过的计划不补跑。")
                wrapMode: Text.Wrap
                textColor: FluTheme.fontSecondaryColor
            }
            FluText {
                visible: HistoryServiceController.error !== ""
                Layout.fillWidth: true
                text: HistoryServiceController.error
                wrapMode: Text.Wrap
                textColor: FluTheme.primaryColor
            }
            FluText {
                visible: HistoryServiceController.diagnostic !== ""
                Layout.fillWidth: true
                text: HistoryServiceController.diagnostic
                wrapMode: Text.Wrap
                textColor: FluTheme.primaryColor
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 16
                rowSpacing: 10
                enabled: HistoryServiceController.registered && HistoryServiceController.statusKnown && !HistoryServiceController.busy
                FluText { text: qsTr("运行时间（24 小时制）") }
                FluTextBox {
                    id: timeInput
                    Layout.fillWidth: true
                    placeholderText: "01:00"
                    maximumLength: 5
                    validator: RegularExpressionValidator { regularExpression: /([01][0-9]|2[0-3]):[0-5][0-9]/ }
                    Accessible.name: qsTr("运行时间（HH:mm）")
                }
                FluText { text: qsTr("运行周期") }
                FluComboBox {
                    id: cycleInput
                    Layout.fillWidth: true
                    model: [qsTr("每天"), qsTr("每周")]
                    Accessible.name: qsTr("运行周期")
                }
                FluText { text: qsTr("星期"); visible: cycleInput.currentIndex === 1 }
                FluComboBox {
                    id: weekdayInput
                    visible: cycleInput.currentIndex === 1
                    Layout.fillWidth: true
                    model: [qsTr("星期一"), qsTr("星期二"), qsTr("星期三"), qsTr("星期四"), qsTr("星期五"), qsTr("星期六"), qsTr("星期日")]
                    Accessible.name: qsTr("每周运行日")
                }
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: 8
                FluFilledButton {
                    text: qsTr("保存计划")
                    disabled: HistoryServiceController.busy || !HistoryServiceController.statusKnown || !HistoryServiceController.registered
                    onClicked: {
                        if (!timeInput.acceptableInput) { info.showError(qsTr("请输入有效时间，例如 06:30"), 4000); return }
                        HistoryServiceController.save(timeInput.text, cycleInput.currentIndex === 1 ? "weekly" : "daily", weekdayInput.currentIndex + 1)
                    }
                }
                FluButton {
                    text: HistoryServiceController.enabled ? qsTr("暂停服务") : qsTr("启用服务")
                    disabled: HistoryServiceController.busy || !HistoryServiceController.statusKnown || !HistoryServiceController.registered
                    onClicked: HistoryServiceController.setEnabled(!HistoryServiceController.enabled)
                }
                FluButton {
                    text: qsTr("注销服务")
                    visible: HistoryServiceController.registered
                    disabled: HistoryServiceController.busy || !HistoryServiceController.statusKnown
                    onClicked: removeDialog.open()
                }
                FluButton {
                    text: qsTr("注册服务")
                    visible: !HistoryServiceController.registered
                    disabled: HistoryServiceController.busy || !HistoryServiceController.statusKnown
                    onClicked: registerDialog.open()
                }
            }
            FluText { text: qsTr("最近运行"); font: FluTextStyle.BodyStrong }
            FluText {
                visible: HistoryServiceController.logError !== ""
                Layout.fillWidth: true
                text: qsTr("日志读取失败：%1").arg(HistoryServiceController.logError)
                wrapMode: Text.Wrap
                textColor: FluTheme.primaryColor
            }
            ListView {
                id: logs
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 10
                model: HistoryServiceController.runs
                ScrollBar.vertical: FluScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: logs.width - 16
                    height: logText.implicitHeight + 20
                    color: FluTheme.itemHoverColor
                    radius: 6
                    Column {
                        id: logText
                        x: 10; y: 10; width: parent.width - 20
                        spacing: 4
                        FluText {
                            width: parent.width
                            text: (modelData.source === "scheduled" ? qsTr("定时服务") : qsTr("手动同步")) + " · " + window.statusText(modelData.status)
                            font: FluTextStyle.BodyStrong
                        }
                        FluText {
                            width: parent.width
                            text: qsTr("开始：%1 · 结束：%2").arg(window.displayTime(modelData.startedAt)).arg(window.displayTime(modelData.finishedAt))
                            wrapMode: Text.Wrap
                        }
                        FluText {
                            width: parent.width
                            text: qsTr("%1 页 · 读取 %2 · 新增 %3 · 已存在 %4").arg(modelData.pages).arg(modelData.seen).arg(modelData.inserted).arg(modelData.existing)
                            wrapMode: Text.Wrap
                        }
                        FluText {
                            visible: modelData.message !== ""
                            width: parent.width
                            text: modelData.message
                            wrapMode: Text.Wrap
                            textColor: FluTheme.fontSecondaryColor
                        }
                    }
                }
                FluText {
                    anchors.centerIn: parent
                    visible: logs.count === 0 && HistoryServiceController.logError === ""
                    text: qsTr("暂无运行记录")
                    textColor: FluTheme.fontSecondaryColor
                }
            }
        }
        FluContentDialog {
            id: removeDialog
            title: qsTr("注销定时服务")
            message: qsTr("停止后续定时同步？本地历史、导出文件和计划配置会保留。")
            buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
            negativeText: qsTr("取消")
            positiveText: qsTr("注销")
            onPositiveClicked: HistoryServiceController.unregisterService()
        }
        FluContentDialog {
            id: registerDialog
            title: qsTr("注册定时服务")
            implicitWidth: 560
            message: Qt.platform.os === "windows"
                ? qsTr("将安装 Windows Service（LocalService），按保存的计划同步历史，并授予所需文件访问权限。下一步将请求 UAC 授权。是否继续？")
                : qsTr("将为当前用户注册系统定时任务，按保存的计划同步历史。是否继续？")
            buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
            negativeText: qsTr("取消")
            positiveText: qsTr("注册")
            onPositiveClicked: HistoryServiceController.registerService()
        }
    }
    Connections {
        target: HistoryServiceController
        function onStateChanged() {
            if (!HistoryServiceController.busy && !window.draftLoaded) window.restoreDraft()
        }
        function onOperationFinished(message) { window.restoreDraft(); info.showSuccess(message, 4000) }
        function onOperationFailed(message) { info.showError(message, 6000) }
    }
    Component.onCompleted: { restoreDraft(); HistoryServiceController.refresh() }
}
