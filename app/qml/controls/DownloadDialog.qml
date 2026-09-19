import QtQuick
import QtQuick.Layouts
import FluentUI

// 卡片和播放器共用的单次下载选择；偏好仅在设置页更改。
FluContentDialog {
    id: dialog
    property var entry: ({})
    property bool includeVideo: true
    property bool includeAudio: false
    property bool includeDanmaku: true
    property bool includeSubtitles: true
    property int selectedQn: 80
    property string validationMessage: ""
    implicitWidth: 480
    title: qsTr("下载内容")
    positiveText: qsTr("加入下载")
    negativeText: qsTr("取消")
    buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton

    function showFor(item) {
        entry = Object.assign({}, item)
        includeVideo = DownloadController.downloadVideo
        includeAudio = !includeVideo && DownloadController.downloadAudio
        includeDanmaku = DownloadController.downloadDanmaku
        includeSubtitles = DownloadController.downloadSubtitles
        selectedQn = DownloadController.preferredQn
        validationMessage = ""
        open()
    }
    onPositiveClickListener: function() {
        if (!includeVideo && !includeAudio && !includeDanmaku && !includeSubtitles) {
            validationMessage = qsTr("请至少选择一项下载内容")
            return
        }
        DownloadController.enqueue(Object.assign({}, entry, {downloadOptions: {
            video: includeVideo, audio: includeAudio, danmaku: includeDanmaku,
            subtitles: includeSubtitles, qn: selectedQn
        }}))
        close()
    }
    contentDelegate: Component {
        Column {
            spacing: 14
            FluText {
                width: parent.width
                text: String(dialog.entry.title || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font: FluTextStyle.BodyStrong
            }
            FluText {
                width: parent.width
                text: qsTr("选择视频将下载并合并音视频；仅音频保存为 M4A。弹幕保存为 XML，字幕保存为 SRT。")
                wrapMode: Text.Wrap
            }
            Flow {
                width: parent.width
                spacing: 20
                FluCheckBox {
                    objectName: "downloadOptionVideo"
                    text: qsTr("视频")
                    checked: dialog.includeVideo
                    clickListener: function() {
                        dialog.includeVideo = !dialog.includeVideo
                        if (dialog.includeVideo) dialog.includeAudio = false
                    }
                }
                FluCheckBox {
                    objectName: "downloadOptionAudio"
                    text: qsTr("仅音频")
                    checked: dialog.includeAudio
                    clickListener: function() {
                        dialog.includeAudio = !dialog.includeAudio
                        if (dialog.includeAudio) dialog.includeVideo = false
                    }
                }
                FluCheckBox {
                    objectName: "downloadOptionDanmaku"
                    text: qsTr("弹幕")
                    checked: dialog.includeDanmaku
                    clickListener: function() { dialog.includeDanmaku = !dialog.includeDanmaku }
                }
                FluCheckBox {
                    objectName: "downloadOptionSubtitles"
                    text: qsTr("字幕")
                    checked: dialog.includeSubtitles
                    clickListener: function() { dialog.includeSubtitles = !dialog.includeSubtitles }
                }
            }
            RowLayout {
                width: parent.width
                FluText { text: qsTr("清晰度") }
                FluComboBox {
                    objectName: "downloadQuality"
                    Layout.fillWidth: true
                    enabled: dialog.includeVideo
                    model: [qsTr("最高可用"), "1080P", "720P", "480P", "360P"]
                    currentIndex: Math.max(0, [127, 80, 64, 32, 16].indexOf(dialog.selectedQn))
                    onActivated: dialog.selectedQn = [127, 80, 64, 32, 16][currentIndex]
                }
            }
            FluText {
                width: parent.width
                text: qsTr("保存到：%1").arg(DownloadController.downloadDirectory)
                wrapMode: Text.WrapAnywhere
                textColor: FluTheme.fontSecondaryColor
            }
            FluText {
                width: parent.width
                visible: dialog.validationMessage !== ""
                text: dialog.validationMessage
                wrapMode: Text.Wrap
                textColor: "#d13438"
            }
            Item { width: 1; height: 4 }
        }
    }
}
