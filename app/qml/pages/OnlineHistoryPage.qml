import QtQuick
import QtQuick.Controls
import FluentUI
import bbhouse

// 在线历史页(online-history-ui):云端 cursor 历史只读瀑布流,不落库、无分页栏。
// 无限滚动:触底(最后三行阈值)续载 + 布局后补轮;空投影(搜索叠加)自动续链
// 上限 5 轮、轮间 400ms、不依赖滚动/布局事件,达上限呈现"未找到匹配"。
// 工具栏仅一个图标刷新按钮(右缘经宽度同步与瀑布流内容区右缘对齐),点击重置
// 游标与池重拉首页;回顶 FAB/封面预览/主体起播沿用本地历史页行为。
// 搜索:页面独立 searchQuery 提交(对已加载池投影,零网络);刷新与续载后
// 按当前词重新投影(注入属性绑定天然实现切页重放)。
// 渲染:虚拟化网格(history-browser-ui 规格,与本地历史/动态页同构),仅视口与
// 缓冲区内的卡片为活动渲染项。页面可能随导航切走被销毁并在返回时重建
// (app-navigation-shell 的"非活动页面渲染释放"):池/游标/搜索词/滚动位置均
// 保存在 OnlineHistoryController 单例,不因渲染重建而丢失或重新拉取首页。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):初始值读自控制器保留的搜索词
    // (渲染重建后回显,而非清零覆盖);之后每次赋值即断开该绑定,变为普通可写属性。
    property string searchQuery: OnlineHistoryController.searchText
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    readonly property bool searching: queryLower !== ""

    // 云端已加载池(去重后;绑定 Controller 单例,切页保持)
    readonly property var pool: OnlineHistoryController.pool
    readonly property var filteredItems: {
        var q = queryLower
        var source = pool
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

    // 空投影自动续链:连续自动轮上限 5(命中/刷新/手势续载均复位)
    property int autoRounds: 0
    readonly property int autoRoundLimit: 5
    // 最近一次失败文案(空串 = 无);空态区按其呈现可重试提示
    property string lastError: ""

    // 瀑布流内容区右缘到页面右缘的距离(刷新按钮对齐用),按虚拟化网格的实际列数
    // 与卡片宽度换算(与 grid 的列/宽度公式保持一致口径)
    readonly property int contentAreaWidth: Math.max(0, width - 48)
    readonly property int gridColumns: Math.max(1, Math.floor(contentAreaWidth / 316))
    readonly property real gridCardWidth: Math.min(300, contentAreaWidth - 16)
    readonly property real gridTotalWidth: gridColumns * (gridCardWidth + 16)
    readonly property int gridRightInset:
        24 + Math.ceil(Math.max(0, contentAreaWidth - gridTotalWidth) / 2)

    // 稳定增量模型:按 videoKey 逐项 diff,续载/去重/搜索投影更新时不打乱
    // 已渲染卡片实例与滚动位置(与 PopularPage 现有实现同构)。
    ListModel { id: cards_model; dynamicRoles: true }
    property bool stateReady: false
    property bool scrollRestored: false
    property bool restoringScroll: false
    property var liveAnchor: ({})
    property var pendingAnchor: ({})
    function captureAnchor() {
        if (!cards_model.count || grid.cellHeight <= 0) return ({})
        var row = Math.max(0, Math.floor((grid.contentY - grid.originY) / grid.cellHeight))
        var index = Math.min(cards_model.count - 1, row * gridColumns)
        return {key: String(cards_model.get(index).cardData.videoKey), index: index,
                fraction: (grid.contentY - grid.originY) / grid.cellHeight - row}
    }
    function queueRestore(anchor) {
        if (!restoringScroll) pendingAnchor = anchor || ({})
        restoringScroll = true
        Qt.callLater(restoreScroll)
    }
    function restoreScroll() {
        if (!stateReady || grid.height <= 0 || grid.width <= 0) return
        grid.forceLayout()
        grid.measureCards()
        grid.forceLayout()
        var anchor = pendingAnchor
        var index = -1
        if (anchor.key) for (var i = 0; i < cards_model.count; ++i) {
            if (String(cards_model.get(i).cardData.videoKey) === anchor.key) { index = i; break }
        }
        var y = scrollRestored ? grid.contentY : OnlineHistoryController.scrollOffset
        if (index < 0 && anchor.key && cards_model.count) index = 0
        if (index >= 0) y = grid.originY + (Math.floor(index / gridColumns) + Number(anchor.fraction || 0)) * grid.cellHeight
        grid.contentY = Math.max(grid.originY, Math.min(y,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
        scrollRestored = true
        restoringScroll = false
        liveAnchor = captureAnchor()
        evaluateAutoChain()
    }
    function syncCards() {
        var entries = page.filteredItems
        queueRestore(scrollRestored ? captureAnchor() : OnlineHistoryController.scrollAnchor)
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            if (i < cards_model.count && cards_model.get(i).cardData.videoKey !== entry.videoKey)
                cards_model.remove(i, cards_model.count - i)
            if (i >= cards_model.count) cards_model.append({cardData: entry})
            else if (JSON.stringify(cards_model.get(i).cardData) !== JSON.stringify(entry))
                cards_model.setProperty(i, "cardData", entry)
        }
        if (cards_model.count > entries.length) cards_model.remove(entries.length, cards_model.count - entries.length)
    }
    onFilteredItemsChanged: if (stateReady) Qt.callLater(syncCards)
    onGridColumnsChanged: if (stateReady) queueRestore(liveAnchor)

    FluInfoBar {
        id: info_bar

        root: page
    }

    // 冻结标题带(高 56 / 边距 24 / 垂直居中,统一规格)
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
            text: qsTr("在线历史")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
        }
    }

    // 工具栏:仅一个仅图标刷新按钮(无文字),右缘与瀑布流内容区右缘对齐
    FluIconButton {
        id: btn_refresh

        width: 34
        height: 34
        iconSource: FluentIcons.Refresh
        iconSize: 14
        enabled: !OnlineHistoryController.busy
        anchors {
            top: title_band.bottom
            topMargin: 4
            right: parent.right
            rightMargin: page.gridRightInset
        }
        onClicked: {
            page.autoRounds = 0
            page.lastError = ""
            grid.contentY = 0  // 刷新后滚动位置回顶
            OnlineHistoryController.refresh()
        }
    }

    // ---- 内容区:虚拟化瀑布流网格(无分页栏) ----
    Item {
        id: content_area

        anchors {
            top: btn_refresh.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            bottomMargin: 8
            leftMargin: 24
            rightMargin: 24
        }

        GridView {
            id: grid
            objectName: "onlineHistoryGrid"
            onHeightChanged: if (page.stateReady) page.queueRestore(page.liveAnchor)
            onCellHeightChanged: if (page.stateReady) page.queueRestore(page.liveAnchor)

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: page.gridColumns * cellWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: page.gridCardWidth + 16
            // 同一页面生命周期内只增高,避免矮卡片进入缓冲区导致整表反复收缩。
            property real measuredCardHeight: 0
            cellHeight: (measuredCardHeight || Math.ceil(page.gridCardWidth * 9 / 16) + 140) + 16
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
            onMovementStarted: {
                page.lastError = ""
                page.autoRounds = 0
                page.evaluateAutoChain()
            }
            onContentYChanged: {
                if (!page.stateReady || page.restoringScroll || !page.scrollRestored) return
                page.liveAnchor = page.captureAnchor()
                page.maybeLoadMore()
            }

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
                    width: page.gridCardWidth
                    height: implicitHeight
                    cardItem: cell.cardData
                    onAuthorClicked: function (author) {
                        AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                    }
                    onCoverClicked: function (sourceItem) {
                        // 点击封面 → 仅预览原图,不触发跳转(互斥契约)
                        cover_preview.show(card.baseUrl, sourceItem)
                    }
                }
            }

            // 列表底部状态条:续载加载中 / 已到底 / 失败可重试(随内容滚动)
            footer: Item {
                width: grid.width
                height: 44
                Row {
                    spacing: 10
                    anchors.centerIn: parent
                    FluProgressRing {
                        indeterminate: true
                        strokeWidth: 3
                        width: 20
                        height: 20
                        visible: OnlineHistoryController.busy && page.pool.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FluText {
                        visible: OnlineHistoryController.busy && page.pool.length > 0
                        text: qsTr("正在加载...")
                        textColor: FluTheme.fontSecondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FluText {
                        visible: OnlineHistoryController.ended && page.pool.length > 0
                        text: qsTr("已经到底了")
                        textColor: FluTheme.fontSecondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FluText {
                        visible: !OnlineHistoryController.busy && !OnlineHistoryController.ended &&
                                 page.pool.length > 0 && page.lastError !== ""
                        text: qsTr("加载失败,滚动或点击右上角刷新重试")
                        textColor: FluTheme.fontSecondaryColor
                        anchors.verticalCenter: parent.verticalCenter
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

        // 空态:未拉到任何条目(空结果/登录失效/失败可重试)vs 搜索无匹配
        FluText {
            anchors.centerIn: parent
            visible: page.filteredItems.length === 0 && page.pool.length === 0
            text: {
                if (OnlineHistoryController.busy) return qsTr("正在加载云端历史...")
                if (page.lastError !== "") {
                    return OnlineHistoryController.unauthorized
                            ? qsTr("登录失效,点击右上角刷新重试")
                            : qsTr("加载失败,点击右上角刷新重试")
                }
                return qsTr("暂无云端观看记录")
            }
            textColor: FluTheme.fontSecondaryColor
        }
        FluText {
            anchors.centerIn: parent
            visible: page.filteredItems.length === 0 && page.pool.length > 0 && page.searching
            // 链式推进已无法继续(达轮上限或已到底)即呈现未找到匹配
            text: (OnlineHistoryController.ended || page.autoRounds >= page.autoRoundLimit)
                  && !OnlineHistoryController.busy
                      ? qsTr("未找到匹配(已载 %1 条)").arg(String(page.pool.length))
                      : qsTr("正在查找匹配...")
            textColor: FluTheme.fontSecondaryColor
        }
    }

    // ---- 空投影自动续链(轮间 400ms;不依赖滚动或布局事件) ----
    Timer {
        id: auto_timer

        interval: 400
        onTriggered: {
            if (!page.stateReady || !page.needsFill() || page.lastError !== "" || OnlineHistoryController.busy ||
                OnlineHistoryController.ended || page.autoRounds >= page.autoRoundLimit) return
            page.autoRounds++
            OnlineHistoryController.loadMore()
        }
    }

    // 链式推进入口:投影命中即复位;搜索中且未到底才续链(重复 evaluate 借
    // restart 去重,轮计数只在真正发起 loadMore 时累加)
    function needsFill() {
        return grid.contentHeight <= grid.height || page.filteredItems.length === 0
    }
    function evaluateAutoChain() {
        if (!stateReady || restoringScroll) return
        if (!needsFill() || OnlineHistoryController.ended || lastError !== "") {
            auto_timer.stop()
            return
        }
        if (OnlineHistoryController.busy || autoRounds >= autoRoundLimit) return
        auto_timer.restart()
    }

    // 触底续载:最后三行阈值内(行高按 封面 169 + 信息区约 150 ≈ 320 估)自动请求
    // 下一页;用户手势续载同时复位自动轮计数(从保留游标恢复)
    function maybeLoadMore() {
        if (!stateReady || restoringScroll || needsFill() || OnlineHistoryController.busy || OnlineHistoryController.ended) return
        var threshold = 3 * 320
        if (grid.contentY + grid.height >= grid.contentHeight - threshold) {
            page.lastError = ""
            page.autoRounds = 0
            OnlineHistoryController.loadMore()
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
            bottom: parent.bottom
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
        target: OnlineHistoryController

        function onPoolChanged() {
            page.lastError = ""
            page.evaluateAutoChain()
        }
        function onBusyChanged() {
            page.evaluateAutoChain()
        }
        function onEndedChanged() {
            page.evaluateAutoChain()
        }
        function onLoadFailed(message) {
            page.lastError = message
            info_bar.showError(message, 3000)
            page.evaluateAutoChain()
        }
    }

    // 重新提交搜索(含空词恢复):写回控制器供渲染释放/重建间保留,复位自动轮后
    // 按新词重新链式投影
    onSearchQueryChanged: {
        if (!stateReady) return
        pendingAnchor = ({})
        liveAnchor = ({})
        OnlineHistoryController.scrollAnchor = ({})
        OnlineHistoryController.scrollOffset = 0
        scrollRestored = false
        OnlineHistoryController.searchText = searchQuery
        page.autoRounds = 0
        page.evaluateAutoChain()
    }

    // 渲染重建时若已保留数据(切页返回)则不重拉首页,仅从未拉取过才发起首页请求
    Component.onCompleted: {
        stateReady = true
        syncCards()
        OnlineHistoryController.ensureLoaded()
    }

    // 渲染即将释放(页面切走)前记忆滚动位置,供下次重建后首次 syncCards() 读回
    Component.onDestruction: {
        if (stateReady && scrollRestored) {
            OnlineHistoryController.scrollAnchor = restoringScroll ? pendingAnchor : captureAnchor()
            OnlineHistoryController.scrollOffset = Math.max(0, grid.contentY - grid.originY)
        }
    }
}
