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
// 按当前词重新投影(注入属性绑定天然实现切页重放)。页面由 MainWindow 常驻
// 缓存,池在 OnlineHistoryController 单例中,切页再返回列表与筛选均保持。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):标题或 UP 主不分大小写子串,空词恢复全量
    property string searchQuery: ""
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

    // ---- 瀑布流列参数(与 LocalHistoryPage 同款):列宽 300、步距 316、整体居中 ----
    readonly property int columnStride: 316
    readonly property int columnGap: 16
    readonly property int viewportWidth: width - 48
    readonly property int columnCount: Math.max(1, Math.floor((viewportWidth + columnGap) / columnStride))
    readonly property int gridWidth: Math.max(0, columnCount * columnStride - columnGap)
    // 瀑布流内容区右缘到页面右缘的距离(刷新按钮对齐用的宽度同步口径;取整方向与
    // masonry.x 的 floor 相对,故右缘用 ceil)
    readonly property int gridRightInset:
        24 + Math.ceil(Math.max(0, viewportWidth - gridWidth) / 2)
    // 列数变化(窗口宽度变化)时重排瀑布流(columnCount 定义在页级,masonry 内
    // 无同名属性,故重排触发挂在这里)
    onColumnCountChanged: masonry.relayout()

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
            scroll_view.contentY = 0  // 刷新后滚动位置回顶
            OnlineHistoryController.refresh()
        }
    }

    // ---- 内容区:瀑布流滚动容器(无分页栏) ----
    Flickable {
        id: scroll_view

        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: flow_footer.y + flow_footer.height + 12
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
        onContentYChanged: page.maybeLoadMore()

        // 瀑布流:最短列放置自绘(列宽 300 定宽、整体居中、无横向滚动),
        // 与 LocalHistoryPage 同构(FluStaggeredLayout 无居中且刷新易错位)
        Item {
            id: masonry

            width: parent.width
            height: Math.max(contentHeight, 1)
            x: Math.max(0, Math.floor((width - gridWidth) / 2))
            readonly property int columnWidth: 300
            readonly property int rowGap: 16
            property real contentHeight: 0

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
                    if (i < columnCount) {
                        col = i
                        top = 0
                        heights.push(card.height)
                    } else {
                        var minHeight = Math.min.apply(null, heights)
                        col = heights.indexOf(minHeight)
                        top = minHeight + rowGap
                        heights[col] = top + card.height
                    }
                    card.x = col * columnStride
                    card.y = top
                }
                masonry.contentHeight = Math.max.apply(null, heights)
            }

            onWidthChanged: relayout()
            // 布局后补轮:新卡片落位、内容高度增长后复查触底阈值(不足一屏时链式补齐)
            onContentHeightChanged: Qt.callLater(page.maybeLoadMore)

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

        // 列表底部状态条:续载加载中 / 已到底 / 失败可重试
        Item {
            id: flow_footer

            width: parent.width
            height: 44
            y: masonry.y + masonry.height + 8
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

        ScrollBar.vertical: FluScrollBar {}
    }

    // ---- 空投影自动续链(轮间 400ms;不依赖滚动或布局事件) ----
    Timer {
        id: auto_timer

        interval: 400
        onTriggered: {
            if (page.filteredItems.length > 0 || OnlineHistoryController.busy ||
                OnlineHistoryController.ended || page.autoRounds >= page.autoRoundLimit) return
            page.autoRounds++
            OnlineHistoryController.loadMore()
        }
    }

    // 链式推进入口:投影命中即复位;搜索中且未到底才续链(重复 evaluate 借
    // restart 去重,轮计数只在真正发起 loadMore 时累加)
    function evaluateAutoChain() {
        if (page.filteredItems.length > 0) {
            page.autoRounds = 0
            auto_timer.stop()
            return
        }
        if (!page.searching || OnlineHistoryController.ended) {
            auto_timer.stop()
            return
        }
        if (OnlineHistoryController.busy) return
        if (page.autoRounds >= page.autoRoundLimit) {
            auto_timer.stop()
            return
        }
        auto_timer.restart()
    }

    // 触底续载:最后三行阈值内(行高按 封面 169 + 信息区约 150 ≈ 320 估)自动请求
    // 下一页;用户手势续载同时复位自动轮计数(从保留游标恢复)
    function maybeLoadMore() {
        if (OnlineHistoryController.busy || OnlineHistoryController.ended) return
        var threshold = 3 * 320
        if (scroll_view.contentY + scroll_view.height >= scroll_view.contentHeight - threshold) {
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

    // 重新提交搜索(含空词恢复):复位自动轮后按新词重新链式投影
    onSearchQueryChanged: {
        page.autoRounds = 0
        page.evaluateAutoChain()
    }

    Component.onCompleted: {
        OnlineHistoryController.refresh()
    }
}
