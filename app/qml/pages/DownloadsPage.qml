import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import FluentUI
import bbhouse

FluPage {
    id: page
    padding: 0
    property string searchQuery: ""
    property bool showingLibrary: false
    readonly property var sourceItems: showingLibrary ? DownloadController.library : DownloadController.downloading
    readonly property var filteredItems: {
        var q = searchQuery.trim().toLowerCase()
        return sourceItems.filter(function(entry) {
            return q === "" || String(entry.title || "").toLowerCase().indexOf(q) >= 0
                || String(entry.authorName || "").toLowerCase().indexOf(q) >= 0
        })
    }
    property int pageIndex: 1
    readonly property int pageSize: 30
    readonly property int totalPages: Math.max(1, Math.ceil(filteredItems.length / pageSize))
    readonly property var pageItems: filteredItems.slice((pageIndex - 1) * pageSize, pageIndex * pageSize)
    readonly property int columnCount: Math.max(1, Math.floor((width - 48 + 16) / 316))
    readonly property int gridWidth: columnCount * 316 - 16
    onColumnCountChanged: Qt.callLater(masonry.relayout)
    onSearchQueryChanged: selectPage(1)
    onShowingLibraryChanged: {
        selectPage(1)
        if (showingLibrary) DownloadController.refreshLibrary()
    }
    onTotalPagesChanged: if (pageIndex > totalPages) selectPage(totalPages)
    onVisibleChanged: if (visible) DownloadController.refreshLibrary()
    Component.onCompleted: DownloadController.refreshLibrary()

    function selectPage(index) {
        pageIndex = Math.max(1, Math.min(index, totalPages))
        scroll_view.contentY = 0
    }
    function playLocal(entry) {
        // 起播时再次验证文件，不依赖上一次刷新得到的存在状态。
        var local = DownloadController.localEntry(String(entry.id))
        if (!local.localPath) {
            DownloadController.refreshLibrary()
            return
        }
        PlayerController.openWith([local])
        FluRouter.navigate("/player")
    }
    function canPlay(entry) {
        return entry.playable === true || entry.available === true
    }
    function displayEntry(entry) {
        return Object.assign({}, entry, {business: "local", progress: 0, invalid: false,
            subtitle: showingLibrary && !canPlay(entry)
                ? String(entry.availabilityMessage || qsTr("本地文件不存在")) : ""})
    }

    FluMenu {
        id: download_menu
        objectName: "downloadContextMenu"
        property var entry: ({})
        FluMenuItem {
            objectName: "downloadRemoveAction"
            text: qsTr("移除")
            onClicked: remove_dialog.showFor(download_menu.entry)
        }
    }
    FluContentDialog {
        id: remove_dialog
        objectName: "downloadRemoveDialog"
        property var entry: ({})
        property bool deleteFiles: false
        implicitWidth: 460
        title: qsTr("移除下载记录")
        positiveText: qsTr("移除")
        negativeText: qsTr("取消")
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        function showFor(item) {
            entry = Object.assign({}, item)
            deleteFiles = false
            open()
        }
        onPositiveClicked: DownloadController.removeRecord(String(entry.id), deleteFiles)
        contentDelegate: Component {
            Column {
                spacing: 14
                FluText {
                    width: parent.width
                    text: String(remove_dialog.entry.title || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font: FluTextStyle.BodyStrong
                }
                FluText {
                    width: parent.width
                    text: qsTr("移除后将不再显示此记录。下载中的任务会先停止。")
                    wrapMode: Text.Wrap
                }
                FluCheckBox {
                    objectName: "downloadRemoveFiles"
                    text: qsTr("删除物理文件")
                    checked: remove_dialog.deleteFiles
                    clickListener: function() { remove_dialog.deleteFiles = !remove_dialog.deleteFiles }
                }
                FluText {
                    width: parent.width
                    text: qsTr("默认保留文件。勾选后将删除媒体及关联的 XML 弹幕、SRT 字幕；下载中的临时文件也会删除。")
                    wrapMode: Text.Wrap
                    textColor: FluTheme.fontSecondaryColor
                }
                Item { width: 1; height: 4 }
            }
        }
    }

    FileDialog {
        id: import_dialog
        title: qsTr("导入本地媒体")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("媒体文件 (*.mp4 *.mkv *.flv *.webm *.mov *.avi *.m4v *.ts *.m4a *.mp3 *.aac *.flac *.ogg *.wav)"), qsTr("所有文件 (*)")]
        onAccepted: {
            DownloadController.importFiles(selectedFiles)
            page.showingLibrary = true
        }
    }
    Item {
        id: title_band
        height: 56
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 24; rightMargin: 24 }
        FluText {
            text: qsTr("下载管理")
            font: FluTextStyle.Title
            anchors { left: parent.left; right: import_button.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
            elide: Text.ElideRight
        }
        FluButton {
            id: import_button
            objectName: "downloadImportButton"
            text: qsTr("导入")
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            onClicked: import_dialog.open()
        }
    }
    Flow {
        id: toolbar
        anchors { left: parent.left; right: parent.right; top: title_band.bottom; leftMargin: 24; rightMargin: 24; topMargin: 8 }
        spacing: 12
        FluToggleButton {
            text: qsTr("下载中 (%1)").arg(String(DownloadController.downloading.length))
            checked: !page.showingLibrary
            clickListener: function() { page.showingLibrary = false }
        }
        FluToggleButton {
            text: qsTr("媒体库 (%1)").arg(String(DownloadController.library.length))
            checked: page.showingLibrary
            clickListener: function() { page.showingLibrary = true }
        }
        FluButton {
            text: qsTr("检测本地文件")
            visible: page.showingLibrary
            onClicked: DownloadController.refreshLibrary()
        }
    }
    Flickable {
        id: scroll_view
        anchors { left: parent.left; right: parent.right; top: toolbar.bottom; bottom: pagination.top;
            leftMargin: 24; rightMargin: 24; topMargin: 16; bottomMargin: 12 }
        clip: true
        contentWidth: width
        contentHeight: masonry.height + 12
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FluScrollBar {}
        Item {
            id: masonry
            width: parent.width
            x: Math.max(0, Math.floor((width - page.gridWidth) / 2))
            height: Math.max(1, contentHeight)
            property real contentHeight: 0
            function relayout() {
                var heights = []
                for (var n = 0; n < page.columnCount; ++n) heights.push(0)
                for (var i = 0; i < cards.count; ++i) {
                    var card = cards.itemAt(i)
                    if (!card) continue
                    var col = heights.indexOf(Math.min.apply(null, heights))
                    card.x = col * 316
                    card.y = heights[col]
                    heights[col] += card.height + 16
                }
                contentHeight = Math.max(0, Math.max.apply(null, heights) - 16)
            }
            onWidthChanged: Qt.callLater(relayout)
            Repeater {
                id: cards
                objectName: "downloadCards"
                model: page.pageItems
                onCountChanged: Qt.callLater(masonry.relayout)
                delegate: Column {
                    id: task_card
                    required property var modelData
                    property int liveProgress: Number(modelData.progress || 0)
                    property string liveMessage: String(modelData.message || modelData.statusText || "")
                    width: 300
                    spacing: 8
                    onHeightChanged: Qt.callLater(masonry.relayout)
                    Component.onCompleted: Qt.callLater(masonry.relayout)
                    Connections {
                        target: DownloadController
                        function onTaskProgress(id, message, progress) {
                            if (String(id) !== String(task_card.modelData.id)) return
                            task_card.liveProgress = progress
                            task_card.liveMessage = message
                        }
                    }
                    HistoryCard {
                        objectName: "downloadCard"
                        width: parent.width
                        height: implicitHeight
                        cardItem: page.displayEntry(task_card.modelData)
                        authorNavigationEnabled: false
                        showHistoryTime: false
                        downloadEnabled: false
                        contextMenuOverride: true
                        onContextMenuRequested: function(sourceItem) {
                            download_menu.entry = task_card.modelData
                            download_menu.popup(sourceItem)
                        }
                        playbackOverride: true
                        playbackEnabled: page.showingLibrary && page.canPlay(task_card.modelData)
                        statisticsText: page.showingLibrary
                            ? (page.canPlay(task_card.modelData) ? qsTr("可本地播放") : qsTr("仅附件或媒体文件不可用"))
                            : task_card.liveMessage
                        onPlaybackRequested: page.playLocal(task_card.modelData)
                        onCoverClicked: function(sourceItem) {
                            if (page.showingLibrary && page.canPlay(task_card.modelData)) page.playLocal(task_card.modelData)
                            else if (baseUrl !== "") cover_preview.show(baseUrl, sourceItem)
                        }
                    }
                    Column {
                        width: parent.width
                        visible: !page.showingLibrary
                        spacing: 8
                        ProgressBar {
                            objectName: "downloadProgressBar"
                            width: parent.width
                            from: 0; to: 100
                            value: Math.max(0, task_card.liveProgress)
                            indeterminate: task_card.liveProgress < 0
                        }
                        FluText {
                            width: parent.width
                            text: String(task_card.modelData.error || (task_card.modelData.state === "failed" ? task_card.modelData.message : "") || "")
                            visible: text !== ""
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            textColor: FluTheme.fontSecondaryColor
                        }
                        Row {
                            spacing: 8
                            readonly property bool stopped: ["failed", "canceled", "cancelled", "interrupted"].indexOf(String(task_card.modelData.state)) >= 0
                            FluButton {
                                objectName: "downloadRetryButton"
                                text: qsTr("重试")
                                visible: parent.stopped
                                onClicked: DownloadController.retry(String(task_card.modelData.id))
                            }
                            FluButton {
                                objectName: "downloadCancelButton"
                                text: qsTr("取消")
                                visible: !parent.stopped
                                onClicked: DownloadController.cancel(String(task_card.modelData.id))
                            }
                        }
                    }
                }
            }
        }
    }
    FluText {
        objectName: "downloadEmptyState"
        anchors.centerIn: scroll_view
        width: scroll_view.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: page.filteredItems.length === 0
        text: page.searchQuery.trim() !== "" ? qsTr("未找到匹配的下载记录")
              : page.showingLibrary ? qsTr("媒体库为空，可导入本地视频或等待下载完成") : qsTr("暂无下载任务，可从视频卡片右键菜单或播放器添加")
        textColor: FluTheme.fontSecondaryColor
    }
    Row {
        id: pagination
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 12 }
        height: visible ? implicitHeight : 0
        visible: page.filteredItems.length > 0
        spacing: 12
        FluButton {
            text: qsTr("上一页")
            enabled: page.pageIndex > 1
            onClicked: page.selectPage(page.pageIndex - 1)
        }
        FluText {
            text: qsTr("第 %1 / %2 页 · 共 %3 条").arg(String(page.pageIndex)).arg(String(page.totalPages)).arg(String(page.filteredItems.length))
            anchors.verticalCenter: parent.verticalCenter
        }
        FluButton {
            text: qsTr("下一页")
            enabled: page.pageIndex < page.totalPages
            onClicked: page.selectPage(page.pageIndex + 1)
        }
    }
    CoverPreviewOverlay { id: cover_preview; anchors.fill: parent; z: 900 }
}
