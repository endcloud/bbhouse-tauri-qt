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
// 页面在后台保留阈值内缓存；超时重建后重新加载，后台同步不受影响。
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
    // 标题栏搜索投影(由 MainWindow 注入;筛选仅作用于 filteredItems 绑定)。
    // 初始值读自控制器保留的搜索词(渲染重建后回显,而非清零覆盖)。
    property string searchQuery: HistoryController.searchText
    // 分页口径的总条数(未筛选;搜索不改变分页)
    property int totalItemCount: 0

    // 仅当前分页:标题或 UP 主不分大小写子串匹配,任一命中即显示
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    onSearchQueryChanged: HistoryController.searchText = searchQuery
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

    // 稳定增量模型:按 videoKey 逐项 diff,翻页/搜索投影更新时不打乱已渲染
    // 卡片实例与滚动位置(与 OnlineHistoryPage/PopularPage 现有实现同构)。
    ListModel { id: cards_model; dynamicRoles: true }
    function syncCards() {
        var entries = page.filteredItems
        var scrollY = grid.contentY
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            if (i < cards_model.count && cards_model.get(i).cardData.videoKey !== entry.videoKey)
                cards_model.remove(i, cards_model.count - i)
            if (i >= cards_model.count) cards_model.append({cardData: entry})
            else if (JSON.stringify(cards_model.get(i).cardData) !== JSON.stringify(entry))
                cards_model.setProperty(i, "cardData", entry)
        }
        if (cards_model.count > entries.length)
            cards_model.remove(entries.length, cards_model.count - entries.length)
        // 翻页/刷新时回顶(与原分页栏行为一致);同页内搜索投影变化保留滚动位置。
        grid.contentY = Math.max(grid.originY, Math.min(scrollY,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }
    onFilteredItemsChanged: Qt.callLater(syncCards)

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

    // ---- 内容区:虚拟化瀑布流网格 ----
    Item {
        id: content_area

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

        readonly property int gridColumns: Math.max(1, Math.floor(width / 316))
        readonly property real gridCardWidth: Math.min(300, width - 16)

        GridView {
            id: grid

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: content_area.gridColumns * cellWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: content_area.gridCardWidth + 16
            // 同一页面生命周期内只增高(与 OnlineHistoryPage/PopularPage 同构)。
            property real measuredCardHeight: 0
            cellHeight: (measuredCardHeight || Math.ceil(content_area.gridCardWidth * 9 / 16) + 140) + 16
            function measureCards() {
                var tallest = measuredCardHeight
                var delegates = contentItem.children
                for (var i = 0; i < delegates.length; ++i) {
                    if (delegates[i].cardHeight !== undefined)
                        tallest = Math.max(tallest, delegates[i].cardHeight)
                }
                measuredCardHeight = tallest
            }
            model: cards_model
            cacheBuffer: cellHeight

            delegate: Item {
                id: cell
                required property var cardData
                readonly property real cardHeight: card.implicitHeight
                onCardHeightChanged: Qt.callLater(grid.measureCards)
                Component.onCompleted: Qt.callLater(grid.measureCards)
                width: grid.cellWidth
                height: grid.cellHeight

                HistoryCard {
                    id: card

                    anchors.horizontalCenter: parent.horizontalCenter
                    width: content_area.gridCardWidth
                    height: implicitHeight
                    cardItem: cell.cardData
                    showRecordedBadge: true
                    onAuthorClicked: function (author) {
                        AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                    }
                    onCoverClicked: function (sourceItem) {
                        // 点击封面 → 仅预览原图,不触发跳转(互斥契约)
                        cover_preview.show(card.baseUrl, sourceItem)
                    }
                }
            }

            ScrollBar.vertical: FluScrollBar {
                parent: grid.parent
                anchors {
                    top: parent.top
                    right: parent.right
                    bottom: parent.bottom
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
                grid.contentY = 0
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
        opacity: grid.contentY > grid.height + 4 ? 1 : 0
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

        target: grid
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
        // 渲染重建时请求上次记忆的页码(而非硬编码首页),避免切走再切回时
        // 误跳回第一页;真正的首次启动该值即为 1。
        HistoryController.loadPage(HistoryController.lastRequestedPage)
    }
}
