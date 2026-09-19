import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import bbhouse

FluPage {
    id: page
    padding: 0

    property string searchQuery: ""
    property int pageIndex: 1
    property string pendingSeasonId: ""
    readonly property int pageSize: 30
    readonly property bool popular: PopularController.activeTab === "popular"
    readonly property bool weekly: PopularController.activeTab === "weekly"
    readonly property bool ranking: PopularController.activeTab === "ranking"
    readonly property string selectionKey: PopularController.activeTab + ":" +
        (weekly ? PopularController.weeklyNumber : ranking ? PopularController.rankingRid : "")
    readonly property var filteredItems: {
        var q = searchQuery.trim().toLowerCase()
        return PopularController.pool.filter(function(item) {
            return !q || String(item.title || "").toLowerCase().indexOf(q) >= 0 ||
                String(item.authorName || "").toLowerCase().indexOf(q) >= 0
        })
    }
    readonly property int totalPages: Math.max(1, Math.ceil(filteredItems.length / pageSize))
    readonly property var visibleItems: popular ? filteredItems :
        filteredItems.slice((pageIndex - 1) * pageSize, pageIndex * pageSize)
    // 保持同一模型对象；状态通知和续载不能重建已有封面或重置滚动。
    ListModel { id: cardsModel; dynamicRoles: true }
    function syncCards() {
        var entries = visibleItems
        var scrollY = grid.contentY
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            if (i < cardsModel.count && cardsModel.get(i).cardData.videoKey !== entry.videoKey)
                cardsModel.remove(i, cardsModel.count - i)
            if (i >= cardsModel.count) cardsModel.append({cardData: entry})
            else if (JSON.stringify(cardsModel.get(i).cardData) !== JSON.stringify(entry))
                cardsModel.setProperty(i, "cardData", entry)
        }
        if (cardsModel.count > entries.length) cardsModel.remove(entries.length, cardsModel.count - entries.length)
        var layoutKey = selectionKey + ":" + pageIndex + ":" + searchQuery
        if (grid.measuredSelectionKey !== layoutKey) {
            grid.measuredSelectionKey = layoutKey
            grid.measuredCardHeight = 0
        }
        grid.forceLayout()
        Qt.callLater(grid.measureCards)
        grid.contentY = Math.max(grid.originY, Math.min(scrollY,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }
    onVisibleItemsChanged: Qt.callLater(syncCards)
    readonly property var categories: [
        {rid: 0, name: qsTr("全部")}, {rid: 1, name: qsTr("动画")},
        {rid: 13, name: qsTr("番剧")}, {rid: 167, name: qsTr("国创")},
        {rid: 3, name: qsTr("音乐")}, {rid: 129, name: qsTr("舞蹈")},
        {rid: 4, name: qsTr("游戏")}, {rid: 36, name: qsTr("知识")},
        {rid: 188, name: qsTr("科技数码")}, {rid: 160, name: qsTr("生活")},
        {rid: 211, name: qsTr("美食")}, {rid: 217, name: qsTr("动物")},
        {rid: 119, name: qsTr("鬼畜")}, {rid: 155, name: qsTr("时尚")},
        {rid: 5, name: qsTr("娱乐")}, {rid: 181, name: qsTr("影视")},
        {rid: 177, name: qsTr("纪录片")}, {rid: 23, name: qsTr("电影")},
        {rid: 11, name: qsTr("电视剧")}, {rid: -1, name: qsTr("全站音乐榜")}
    ]

    function selectPage(number) {
        pageIndex = Math.max(1, Math.min(number, totalPages))
        pagination.pageCurrent = pageIndex
        grid.positionViewAtBeginning()
    }
    onSearchQueryChanged: selectPage(1)
    onSelectionKeyChanged: {
        pendingSeasonId = ""
        selectPage(1)
    }
    onTotalPagesChanged: {
        if (pageIndex > totalPages) selectPage(totalPages)
    }
    onVisibleChanged: {
        if (!visible) pendingSeasonId = ""
    }

    function formatCount(value) {
        var number = Math.max(0, Number(value || 0))
        if (number >= 100000000) return qsTr("%1亿").arg((number / 100000000).toFixed(1))
        if (number >= 10000) return qsTr("%1万").arg((number / 10000).toFixed(1))
        return String(number)
    }
    function statistics(item) {
        var parts = []
        if (ranking && Number(item.rank || 0) > 0) parts.push(qsTr("第 %1 名").arg(item.rank))
        if (item.playCount !== undefined) parts.push(qsTr("%1 播放").arg(formatCount(item.playCount)))
        if (item.danmakuCount !== undefined) parts.push(qsTr("%1 弹幕").arg(formatCount(item.danmakuCount)))
        return parts.join(" · ")
    }

    function playSeason(item) {
        pendingSeasonId = String(item.seasonId || "")
        var detail = PopularSeasonController.seasonDetail(Number(pendingSeasonId), false)
        if (detail && detail.seasonId) finishSeason(detail)
        else infoBar.showInfo(qsTr("正在获取剧集信息…"), 3000)
    }
    function finishSeason(detail) {
        if (!pendingSeasonId || String(detail.seasonId) !== pendingSeasonId || !visible) return
        pendingSeasonId = ""
        var episodes = detail.episodes || []
        if (!episodes.length) {
            infoBar.showError(qsTr("暂无可播放分集"), 3000)
            return
        }
        var startIndex = 0
        for (var i = 0; i < episodes.length; ++i) {
            if (String(episodes[i].epId) === String(detail.lastEpId)) startIndex = i
        }
        PlayerController.playSeason({title: detail.title, coverUrl: detail.coverUrl,
            regional: false, lastEpId: detail.lastEpId, lastTimeSeconds: detail.lastTimeSeconds},
            episodes, startIndex)
        FluRouter.navigate("/player")
    }
    Connections {
        target: PopularSeasonController
        function onSeasonDetailReady(detail) { page.finishSeason(detail) }
        function onErrorOccurred(message) {
            if (!page.pendingSeasonId) return
            page.pendingSeasonId = ""
            infoBar.showError(message, 5000)
        }
    }

    FluInfoBar { id: infoBar; root: page }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.bottomMargin: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            FluText { text: qsTr("流行"); font: FluTextStyle.Title; Layout.fillWidth: true }
            FluProgressRing {
                visible: PopularController.busy || (page.weekly && PopularController.periodsBusy)
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                strokeWidth: 3
            }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                Accessible.name: qsTr("刷新当前内容")
                disabled: PopularController.busy
                onClicked: PopularController.refresh()
                FluTooltip { text: qsTr("刷新当前内容"); visible: parent.hovered; delay: 500 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                spacing: 8
                Repeater {
                    model: [{key: "popular", name: qsTr("综合热门")},
                        {key: "weekly", name: qsTr("每周必看")},
                        {key: "precious", name: qsTr("入站必刷")},
                        {key: "ranking", name: qsTr("排行榜")}]
                    delegate: FluToggleButton {
                        id: tabButton
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 54
                        text: modelData.name
                        Binding { target: tabButton.contentItem; property: "elide"; value: Text.ElideRight }
                        FluTooltip { text: tabButton.text; visible: tabButton.hovered; delay: 500 }
                        checked: PopularController.activeTab === modelData.key
                        clickListener: function() { PopularController.selectTab(modelData.key) }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            FluComboBox {
                objectName: "popularRankingSelector"
                visible: page.ranking
                Layout.rightMargin: 12
                Layout.preferredWidth: Math.max(140, Math.min(220, page.width - 490))
                textRole: "name"
                model: page.categories
                currentIndex: {
                    for (var i = 0; i < page.categories.length; ++i)
                        if (page.categories[i].rid === PopularController.rankingRid) return i
                    return 0
                }
                Accessible.name: qsTr("选择排行榜")
                onActivated: function(index) { PopularController.selectRanking(page.categories[index].rid) }
            }
        }
        RowLayout {
            visible: page.weekly
            Layout.fillWidth: true
            FluText { text: qsTr("期数") }
            FluComboBox {
                id: periods
                objectName: "popularWeeklySelector"
                Layout.fillWidth: true
                Layout.maximumWidth: 460
                textRole: "label"
                model: PopularController.weeklyPeriods
                currentIndex: {
                    var entries = PopularController.weeklyPeriods
                    for (var i = 0; i < entries.length; ++i)
                        if (entries[i].number === PopularController.weeklyNumber) return i
                    return -1
                }
                displayText: currentIndex >= 0 ? currentText :
                    PopularController.periodsBusy ? qsTr("正在加载期数…") : qsTr("暂无期数")
                disabled: PopularController.weeklyPeriods.length === 0
                Accessible.name: qsTr("选择每周必看期数")
                onActivated: function(index) {
                    PopularController.selectWeek(PopularController.weeklyPeriods[index].number)
                }
            }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                disabled: PopularController.periodsBusy
                Accessible.name: qsTr("刷新期数目录")
                onClicked: PopularController.refreshPeriods()
                FluTooltip { text: qsTr("刷新期数目录"); visible: parent.hovered; delay: 500 }
            }
        }
        FluText {
            visible: page.weekly && PopularController.periodsError !== ""
            Layout.fillWidth: true
            text: qsTr("期数目录加载失败：%1").arg(PopularController.periodsError)
            textColor: FluTheme.primaryColor
            wrapMode: Text.Wrap
        }
        FluText {
            visible: PopularController.error !== ""
            Layout.fillWidth: true
            text: qsTr("加载失败：%1；可点击刷新或加载更多重试。").arg(PopularController.error)
            textColor: FluTheme.primaryColor
            wrapMode: Text.Wrap
        }
        FluText {
            visible: PopularController.description !== ""
            Layout.fillWidth: true
            text: PopularController.description
            textColor: FluTheme.fontSecondaryColor
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridView {
                id: grid
                objectName: "popularGrid"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                // 与动态页一致：300px 卡片、16px 间距，剩余空间留在两侧。
                readonly property int columns: Math.max(1, Math.floor(parent.width / 316))
                readonly property real cardWidth: Math.min(300, parent.width - 16)
                width: columns * cellWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                cellWidth: cardWidth + 16
                // 同一选择只增高，避免滚动时短卡片进入缓存导致整表反复收缩。
                property string measuredSelectionKey: ""
                property real measuredCardHeight: 0
                cellHeight: (measuredCardHeight || Math.ceil(cardWidth * 9 / 16) + 140) + 16
                function measureCards() {
                    var tallest = measuredCardHeight
                    var delegates = contentItem.children
                    for (var i = 0; i < delegates.length; ++i) {
                        if (delegates[i].cardHeight !== undefined)
                            tallest = Math.max(tallest, delegates[i].cardHeight)
                    }
                    measuredCardHeight = tallest
                }
                onCardWidthChanged: {
                    measuredCardHeight = 0
                    Qt.callLater(measureCards)
                }
                model: cardsModel
                cacheBuffer: cellHeight
                ScrollBar.vertical: FluScrollBar {
                    parent: grid.parent
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                }
                onMovementEnded: {
                    if (page.popular && atYEnd && !page.searchQuery.trim() &&
                            !PopularController.error && PopularController.hasMore && !PopularController.busy)
                        PopularController.loadMore()
                }
                delegate: Item {
                    required property var cardData
                    readonly property real cardHeight: card.implicitHeight
                    onCardHeightChanged: Qt.callLater(grid.measureCards)
                    Component.onCompleted: Qt.callLater(grid.measureCards)
                    width: grid.cellWidth
                    height: grid.cellHeight
                    HistoryCard {
                        id: card
                        objectName: "popularCard"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: grid.cardWidth
                        height: implicitHeight
                        cardItem: cardData
                        showHistoryTime: false
                        showAuthor: cardData.business !== "pgc"
                        statisticsText: page.statistics(cardData)
                        seasonPlaybackEnabled: true
                        onSeasonPlayRequested: function(item) { page.playSeason(item) }
                        onAuthorClicked: function(author) {
                            AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                        }
                        onCoverClicked: function(sourceItem) { coverPreview.show(card.baseUrl, sourceItem) }
                    }
                }
            }
            FluText {
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: page.filteredItems.length === 0
                text: PopularController.busy || (page.weekly && PopularController.periodsBusy)
                    ? qsTr("正在加载流行内容…")
                    : PopularController.error || (page.weekly && PopularController.periodsError)
                    ? qsTr("加载失败，请点击刷新重试。")
                    : page.searchQuery.trim() ? qsTr("已加载内容中没有匹配结果。")
                    : page.weekly && PopularController.weeklyPeriods.length === 0
                    ? qsTr("暂无每周必看期数") : qsTr("暂无内容")
                textColor: FluTheme.fontSecondaryColor
            }
            FluIconButton {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                iconSource: FluentIcons.ChevronUp
                visible: grid.contentY > grid.height
                Accessible.name: qsTr("回到顶部")
                onClicked: grid.positionViewAtBeginning()
            }
        }
        RowLayout {
            visible: page.popular
            Layout.fillWidth: true
            FluText {
                Layout.fillWidth: true
                text: qsTr("搜索仅筛选已加载内容 · 已加载 %1 条").arg(PopularController.pool.length)
                elide: Text.ElideRight
                font: FluTextStyle.Caption
                textColor: FluTheme.fontSecondaryColor
            }
            FluButton {
                visible: PopularController.hasMore
                disabled: PopularController.busy
                text: qsTr("加载更多")
                onClicked: PopularController.loadMore()
            }
            FluText {
                visible: PopularController.loaded && !PopularController.hasMore
                text: qsTr("已加载全部")
            }
        }
        RowLayout {
            visible: !page.popular && page.filteredItems.length > 0
            Layout.fillWidth: true
            spacing: 12
            FluText {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: qsTr("第 %1 / %2 页 · 共 %3 条").arg(page.pageIndex).arg(page.totalPages).arg(page.filteredItems.length)
            }
            FluPagination {
                id: pagination
                // 明确最小宽度，避免内部 Flow 被布局压缩成三行。
                Layout.minimumWidth: implicitWidth
                Layout.preferredWidth: implicitWidth
                pageButtonCount: page.width < 800 ? 3 : 5
                pageCurrent: 1
                itemCount: page.filteredItems.length
                __itemPerPage: page.pageSize
                onRequestPage: function(number, count) { page.selectPage(number) }
            }
        }
    }
    CoverPreviewOverlay { id: coverPreview; anchors.fill: parent; z: 900 }
    Component.onCompleted: PopularController.ensureLoaded()
}
