import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI
import bbhouse

// 本地历史页完整化(history-browser-ui 浏览视图宿主 + 瀑布流契约):
// 冻结标题带 + 同步工具栏 + 状态计数 + 瀑布流卡片(300px 定宽列,列数自适应,
// 最短列放置,整体居中,无横向滚动)+ 分页栏(首页/上一页/下一页/末页 +
// "第 x / y 页 · 显示 a-b 共 n 条")+ 回到顶部 FAB + 封面预览层。
// 搜索:页面独立 searchQuery 投影,仅对当前分页已加载
// 条目的标题/UP 主做不分大小写子串筛选;空词恢复全量。
// 页面实例由 MainWindow 常驻缓存,切走再回来页码/列表/搜索词不重置。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    property bool initialized: false
    onVisibleChanged: if (visible && initialized) HistoryController.loadPage(pageIndex)

    property int pageIndex: 1  // FluPagination 页码从 1 起
    readonly property int pageSize: 30
    // 当前分页已加载条目(pageLoaded 快照;翻页/同步完成时刷新)
    property var pageItems: []
    // 标题栏搜索投影(由 MainWindow 注入;筛选仅作用于 filteredItems 绑定)
    property string searchQuery: ""
    // 分页口径的总条数(未筛选;搜索不改变分页)
    property int totalItemCount: 0

    // 仅当前分页:标题或 UP 主不分大小写子串匹配,任一命中即显示
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    readonly property var filteredItems: {
        var q = queryLower
        var source = pageItems
        if (q === "") return source
        var result = []
        for (var i = 0; i < source.length; i++) {
            var entry = source[i]
            var title = String(entry.title || "").toLowerCase()
            var author = String(entry.authorName || "").toLowerCase()
            if (title.indexOf(q) !== -1 || author.indexOf(q) !== -1) {
                result.push(entry)
            }
        }
        return result
    }

    FluInfoBar {
        id: info_bar

        root: page
    }

    // 冻结标题带(高 56 / 边距 24 / 垂直居中,与设置页对齐)
    Item {
        id: title_band

        height: 56
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        FluText {
            text: qsTr("本地历史")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 8
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            FluButton {
                text: qsTr("服务管理")
                disabled: HistoryServiceController.busy
                onClicked: HistoryServiceController.requestOpen()
            }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                disabled: HistoryController.loading
                Accessible.name: qsTr("刷新本地历史")
                onClicked: HistoryController.loadPage(page.pageIndex)
                FluTooltip { text: qsTr("刷新本地历史"); visible: parent.hovered; delay: 500 }
            }
        }
    }

    // 工具栏:同步按钮 + 状态文本(接 HistoryController)
    RowLayout {
        id: toolbar

        spacing: 12
        anchors {
            top: title_band.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        FluFilledButton {
            id: sync_button
            text: qsTr("同步")
            disabled: HistoryController.syncing || HistoryController.loading
            onClicked: {
                HistoryController.startSync()
            }
        }
        FluText {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: HistoryController.loadError || (HistoryController.syncStatus.length > 0 ? HistoryController.syncStatus
                                                          : (HistoryController.ready ? qsTr("就绪") : qsTr("正在初始化本地库...")))
        }
        FluTextButton {
            id: cancel_sync
            text: qsTr("取消同步")
            visible: HistoryController.syncing
            onClicked: {
                HistoryController.cancelSync()
            }
        }
    }

    // 状态计数(离线优先契约:展示已载入的视频数与观看记录数)
    FluText {
        id: counts_text

        text: qsTr("共 %1 视频 / %2 观看记录")
              .arg(String(HistoryController.videoCount))
              .arg(String(HistoryController.recordCount))
        textColor: FluTheme.fontSecondaryColor
        wrapMode: Text.Wrap
        anchors {
            right: parent.right
            rightMargin: 24
            top: toolbar.bottom
            topMargin: 6
            left: parent.left
            leftMargin: 24
        }
    }

    // ---- 内容区:瀑布流滚动容器 ----
    Flickable {
        id: scroll_view

        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: masonry.y + masonry.height + 12
        anchors {
            top: counts_text.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            bottom: pagination_host.top
            bottomMargin: 8
            leftMargin: 24
            rightMargin: 24
        }

        // 瀑布流:列宽固定 300,步距 316;列数 = floor(可用宽/316) ≥ 1;
        // 卡片逐张放入当前最短列;整体水平居中(容器宽随列数收缩,不横向滚动)。
        // FluStaggeredLayout 不适用:其定位基于 itemWidth+rowSpacing 无居中、
        // 移除/刷新时 itemsInRep 易错位,故按契约自绘最短列布局。
        Item {
            id: masonry

            readonly property int columnWidth: 300
            readonly property int columnGap: 16
            readonly property int rowGap: 16
            readonly property int stride: columnWidth + columnGap
            readonly property int columnCount: Math.max(1, Math.floor((width + columnGap) / stride))
            readonly property int gridWidth: Math.max(0, columnCount * stride - columnGap)
            property real contentHeight: 0

            width: parent.width
            height: Math.max(contentHeight, 1)
            x: Math.max(0, Math.floor((width - gridWidth) / 2))

            function relayout() {
                var count = cards_repeater.count
                if (count === 0) {
                    masonry.contentHeight = 0
                    return
                }
                var heights = []
                for (var i = 0; i < count; i++) {
                    var card = cards_repeater.itemAt(i)
                    if (!card) continue
                    var col, top
                    if (i < masonry.columnCount) {
                        col = i
                        top = 0
                        heights.push(card.height)
                    } else {
                        var minHeight = Math.min.apply(null, heights)
                        col = heights.indexOf(minHeight)
                        top = minHeight + masonry.rowGap
                        heights[col] = top + card.height
                    }
                    card.x = col * masonry.stride
                    card.y = top
                }
                masonry.contentHeight = Math.max.apply(null, heights)
            }

            onColumnCountChanged: relayout()
            onWidthChanged: relayout()

            Repeater {
                id: cards_repeater

                onCountChanged: Qt.callLater(masonry.relayout)

                model: page.filteredItems

                delegate: HistoryCard {
                    onAuthorClicked: function (author) {
                        AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                    }
                    id: card

                    cardItem: modelData
                    showRecordedBadge: true
                    width: masonry.columnWidth
                    height: implicitHeight
                    onImplicitHeightChanged: Qt.callLater(masonry.relayout)
                    Component.onCompleted: Qt.callLater(masonry.relayout)
                    onCoverClicked: function (sourceItem) {
                        // 点击封面 → 仅预览原图,不触发跳转(互斥契约)
                        cover_preview.show(card.baseUrl, sourceItem)
                    }
                }
            }
        }

        // 空态:整库为空 vs 当前页无匹配
        FluText {
            anchors.centerIn: parent
            visible: page.filteredItems.length === 0
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: HistoryController.loading ? qsTr("正在读取本地历史…") :
                HistoryController.loadError ? qsTr("读取失败，请点击刷新重试。") :
                page.pageItems.length === 0 ? qsTr("暂无观看记录,点击「同步」获取") : qsTr("当前页无匹配条目")
            textColor: FluTheme.fontSecondaryColor
        }

        ScrollBar.vertical: FluScrollBar {}
    }

    // ---- 分页栏:首页/上一页/数字/下一页/末页 + 口径文案 ----
    Column {
        id: pagination_host

        width: parent.width - 48
        spacing: 4
        height: visible ? implicitHeight : 0
        anchors {
            bottom: parent.bottom
            bottomMargin: 12
            horizontalCenter: parent.horizontalCenter
        }
        FluText {
            function indicator() {
                var n = page.totalItemCount
                var totalPages = Math.max(1, Math.ceil(n / page.pageSize))
                var from = n === 0 ? 0 : (page.pageIndex - 1) * page.pageSize + 1
                var to = Math.min(page.pageIndex * page.pageSize, n)
                return qsTr("第 %1 / %2 页 · 显示 %3-%4 共 %5 条")
                        .arg(String(page.pageIndex)).arg(String(totalPages))
                        .arg(String(from)).arg(String(to)).arg(String(n))
            }
            text: indicator()
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        FluPagination {
            id: pagination_bar
            enabled: !HistoryController.loading

            width: Math.min(implicitWidth, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            pageButtonCount: page.width < 720 ? 3 : 5

            itemCount: page.totalItemCount
            __itemPerPage: page.pageSize
            header: Component {
                FluToggleButton {
                    text: qsTr("首页")
                    visible: pagination_bar.pageCount > 0
                    disabled: pagination_bar.pageCurrent <= 1
                    clickListener: function () {
                        pagination_bar.calcNewPage(1)
                    }
                }
            }
            footer: Component {
                FluToggleButton {
                    text: qsTr("末页")
                    visible: pagination_bar.pageCount > 1
                    disabled: pagination_bar.pageCurrent >= pagination_bar.pageCount
                    clickListener: function () {
                        pagination_bar.calcNewPage(pagination_bar.pageCount)
                    }
                }
            }
            onRequestPage: function (requestedPage, count) {
                scroll_view.contentY = 0
                HistoryController.loadPage(requestedPage)
            }
        }
    }

    // ---- 回到顶部 FAB(圆形;下滚超过一屏出现;预览层覆盖其上) ----
    FluIconButton {
        id: fab_top

        z: 10
        width: 40
        height: 40
        radius: 20
        iconSource: FluentIcons.ChevronUp
        iconSize: 14
        opacity: scroll_view.contentY > scroll_view.height + 4 ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity {
            NumberAnimation {
                duration: 150
            }
        }
        anchors {
            right: parent.right
            rightMargin: 24
            bottom: pagination_host.top
            bottomMargin: 16
        }
        onClicked: {
            scroll_anim.to = 0
            scroll_anim.restart()
        }
    }
    NumberAnimation {
        id: scroll_anim

        target: scroll_view
        property: "contentY"
        to: 0
        duration: 240
        easing.type: Easing.OutCubic
    }

    // 封面预览层:全页覆盖(含 FAB),最高 z
    CoverPreviewOverlay {
        id: cover_preview

        anchors.fill: parent
        z: 900
    }

    Connections {
        target: HistoryController

        function onPageLoaded(loadedPage, items, total) {
            page.pageIndex = loadedPage
            page.pageItems = items
            page.totalItemCount = total
            pagination_bar.pageCurrent = loadedPage
        }
        function onLoadFailed(message) { info_bar.showError(message, 5000) }
        function onSyncFailed(message) {
            info_bar.showError(message, 3000)
        }
        function onSyncFinished(summary) {
            info_bar.showSuccess(summary, 3000)
            // 同步落库后重读当前页,计数/列表即时反映增量(不跳页)
            HistoryController.loadPage(Math.max(1, page.pageIndex))
        }
    }

    FluContentDialog {
        id: register_service_dialog
        implicitWidth: 560
        title: qsTr("尚未注册定时服务")
        message: Qt.platform.os === "windows"
            ? qsTr("是否安装 Windows Service（LocalService）？默认每天 01:00 同步历史，退出登录后仍可运行。下一步将请求 UAC 授权，并授予所需文件访问权限。")
            : qsTr("是否为当前用户注册系统定时任务？默认每天 01:00 同步历史，注册后可在新窗口中修改时间与周期。")
        buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
        negativeText: qsTr("取消")
        positiveText: qsTr("注册服务")
        onPositiveClicked: HistoryServiceController.registerService()
    }
    Connections {
        target: HistoryServiceController
        function onRegistrationRequired() { register_service_dialog.open() }
        function onOpenRequested() { FluRouter.navigate("/history-service") }
        function onOperationFailed(message) { if (page.visible) info_bar.showError(message, 6000) }
    }
    Component.onCompleted: {
        initialized = true
        HistoryController.loadPage(1)
    }
}
