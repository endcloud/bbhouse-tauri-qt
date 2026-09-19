import QtQuick
import QtQuick.Controls
import FluentUI
import bbhouse

// 关注动态页(dynamics-ui):DynamicApi feed/all offset 翻页只读瀑布流,不落库。
// 六档分类筛选(视频/番剧影视/直播/专栏/动态/其他):紧凑 ToggleButton
// chip 组(点已选档保持选中,互斥自管理),切换即时重投影(Controller 池内
// 过滤,零网络)+ 回顶;档位持久化 App.Dynamics.Filter(枚举名存储,未知回退
// 视频，旧全部档也迁移至视频)。视频档第二级分区行:仅 video 档显示,"全部分区"+ 池内已解析分区去重
// 集合(随解析进度动态补充),离开档清空选择、不跨会话持久化。
// 瀑布流照抄在线历史页自绘方案(300px 列/最短列/居中);触底续载 + 布局后补轮;
// 空投影自动续链上限 5 轮、轮间 400ms、不依赖滚动/布局事件;分区解析未决
// (zoneGateActive)期间暂缓"未找到匹配"终态与自动续拉。刷新保留既有列表
// (Controller 侧成功一轮后整体替换,不闪空白),保持筛选档位。
// 卡片:HistoryCard 扩展键(出现 N 次角标/失效占位);视频主体点击经 openWith
// 走既有播放设施，封面点击预览，右键打开链接。
// 搜索:页面独立 searchQuery 提交(Controller 池上投影,零网络);页面由
// MainWindow 常驻缓存,池/offset/筛选在 DynamicsController 单例,切页再返回全保持。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):标题或 UP 主不分大小写子串,空词恢复全量
    property string searchQuery: ""
    readonly property string queryLower: searchQuery.trim().toLowerCase()

    // ---- 六档分类筛选(档位持久化;旧 all/未知档位回退 video) ----
    readonly property var categoryKeys: ["video", "pgc", "live", "article", "post", "other"]
    readonly property var categoryLabels: [
        qsTr("视频"), qsTr("番剧影视"), qsTr("直播"),
        qsTr("专栏"), qsTr("动态"), qsTr("其他")
    ]
    property string filter: {
        var stored = String(AppPreferences.value("App.Dynamics.Filter", "video"))
        return categoryKeys.indexOf(stored) !== -1 ? stored : "video"
    }

    // ---- 视频分区筛选(仅 video 档;离开档清空,不跨会话持久化) ----
    property string zoneFilter: ""

    // 云端已加载池(去重后;绑定 Controller 单例,切页保持)
    readonly property var pool: DynamicsController.pool
    // 分区集合独立通知并增量更新；已有按钮不随每个 view 回应重建。
    ListModel { id: zone_options }
    function syncZoneOptions() {
        var names = DynamicsController.zoneNames
        for (var i = zone_options.count - 1; i >= 0; --i) {
            if (names.indexOf(zone_options.get(i).zoneName) === -1) zone_options.remove(i)
        }
        for (var j = 0; j < names.length; ++j) {
            if (j < zone_options.count && zone_options.get(j).zoneName === names[j]) continue
            var found = -1
            for (var k = j + 1; k < zone_options.count; ++k) {
                if (zone_options.get(k).zoneName === names[j]) { found = k; break }
            }
            if (found >= 0) zone_options.move(found, j, 1)
            else zone_options.insert(j, {zoneName: names[j]})
        }
    }

    // 当前投影(分类∩分区∩搜索;Controller 池上内存过滤,零网络)
    readonly property var items: DynamicsController.items

    // 空投影自动续链:连续自动轮上限 5(命中/刷新/手势续载均复位)
    property int autoRounds: 0
    readonly property int autoRoundLimit: 5
    // 最近一次失败文案(空串 = 无);空态区按其呈现可重试提示
    property string lastError: ""

    // 紧凑 chip 单选:点已选档保持选中(不进入"全不选"中间态),互斥自管理
    function selectFilter(key) {
        if (page.filter === key) return
        page.filter = key
    }
    function selectZone(key) {
        if (page.zoneFilter === key) return
        page.zoneFilter = key
    }

    onFilterChanged: {
        AppPreferences.setValue("App.Dynamics.Filter", filter)
        if (filter !== "video" && zoneFilter !== "") zoneFilter = ""  // 离开视频档清空分区选择
        DynamicsController.categoryFilter = filter
        scroll_view.contentY = 0  // 切档回顶(即时重排,零网络)
        page.autoRounds = 0
        page.evaluateAutoChain()
    }
    onZoneFilterChanged: {
        DynamicsController.zoneFilter = zoneFilter
        scroll_view.contentY = 0
        page.autoRounds = 0
        page.evaluateAutoChain()
    }
    onQueryLowerChanged: {
        DynamicsController.searchText = queryLower
        page.autoRounds = 0
        page.evaluateAutoChain()
    }

    // ---- 瀑布流列参数(与在线历史页同款):列宽 300、步距 316、整体居中 ----
    readonly property int columnStride: 316
    readonly property int columnGap: 16
    readonly property int viewportWidth: width - 48
    readonly property int columnCount: Math.max(1, Math.floor((viewportWidth + columnGap) / columnStride))
    readonly property int gridWidth: Math.max(0, columnCount * columnStride - columnGap)
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
            id: title_text
            text: qsTr("动态")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 10
            anchors.left: title_text.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter

            FluIconButton {
                id: btn_refresh

                width: 34
                height: 34
                iconSource: FluentIcons.Refresh
                iconSize: 14
                enabled: !DynamicsController.busy
                onClicked: {
                    page.autoRounds = 0
                    page.lastError = ""
                    scroll_view.contentY = 0  // 刷新后滚动位置回顶
                    DynamicsController.refresh()
                }
            }
            // 加载环位于按钮之后，显隐时刷新按钮不会移动。
            FluProgressRing {
                indeterminate: true
                strokeWidth: 3
                width: 22
                height: 22
                visible: DynamicsController.busy
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ---- 工具栏:六档筛选 chips，窄窗独立折行 ----
    Flow {
        id: filter_row

        spacing: 6
        anchors {
            top: title_band.bottom
            topMargin: 4
            left: parent.left
            leftMargin: 24
            right: parent.right
            rightMargin: 24
        }
        Repeater {
            model: page.categoryKeys

            delegate: Row {
                id: category_option
                required property var modelData
                required property int index
                spacing: 6

                // 和专栏按钮一起换行，窄窗时不会留下单独一条分隔线。
                FluDivider {
                    visible: category_option.modelData === "article"
                    orientation: Qt.Vertical
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
                FluToggleButton {
                    text: page.categoryLabels[category_option.index]
                    checked: page.filter === category_option.modelData
                    clickListener: function () {
                        page.selectFilter(category_option.modelData)
                    }
                }
            }
        }

    }

    // 第二级分区行:仅 video 档显示(窄间隙横排紧凑 chips)
    Flow {
        id: zone_row

        spacing: 6
        visible: page.filter === "video"
        anchors {
            top: filter_row.bottom
            topMargin: 6
            left: parent.left
            leftMargin: 24
            right: parent.right
            rightMargin: 24
        }
        FluToggleButton {
            text: qsTr("全部分区")
            checked: page.zoneFilter === ""
            clickListener: function () { page.selectZone("") }
        }
        Repeater {
            objectName: "dynamicsZoneRepeater"
            model: zone_options

            delegate: FluToggleButton {
                required property string zoneName

                text: zoneName
                checked: page.zoneFilter === zoneName
                clickListener: function () {
                    page.selectZone(zoneName)
                }
            }
        }
    }

    // ---- 内容区:瀑布流滚动容器(无分页栏) ----
    Flickable {
        id: scroll_view
        objectName: "dynamicsScrollView"

        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: flow_footer.y + flow_footer.height + 12
        anchors {
            top: zone_row.visible ? zone_row.bottom : filter_row.bottom
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
        // 与在线历史页同构(FluStaggeredLayout 无居中且刷新易错位)
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
                objectName: "dynamicsCardRepeater"

                onCountChanged: Qt.callLater(masonry.relayout)

                model: DynamicsController.cardModel

                delegate: HistoryCard {
                    onAuthorClicked: function (author) {
                        AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                    }
                    id: card

                    required property var cardData
                    cardItem: cardData
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
                    visible: DynamicsController.busy && page.pool.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                }
                FluText {
                    visible: DynamicsController.busy && page.pool.length > 0
                    text: qsTr("正在加载...")
                    textColor: FluTheme.fontSecondaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                FluText {
                    visible: DynamicsController.ended && page.pool.length > 0
                    text: qsTr("已经到底了")
                    textColor: FluTheme.fontSecondaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                FluText {
                    visible: !DynamicsController.busy && !DynamicsController.ended &&
                             page.pool.length > 0 && page.lastError !== ""
                    text: qsTr("加载失败,滚动或点击右上角刷新重试")
                    textColor: FluTheme.fontSecondaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // 空态:未拉到任何条目(空结果/登录失效/失败可重试)vs 投影无匹配
        FluText {
            anchors.centerIn: parent
            visible: page.items.length === 0 && page.pool.length === 0
            text: {
                if (DynamicsController.busy) return qsTr("正在加载动态流...")
                if (page.lastError !== "") {
                    return DynamicsController.unauthorized
                            ? qsTr("登录失效,点击右上角刷新重试")
                            : qsTr("加载失败,点击右上角刷新重试")
                }
                return qsTr("暂无动态")
            }
            textColor: FluTheme.fontSecondaryColor
        }
        FluText {
            anchors.centerIn: parent
            visible: page.items.length === 0 && page.pool.length > 0
            text: {
                // 分区解析未决:暂缓"未找到匹配"终态判定(dynamics-ui 门控契约)
                if (DynamicsController.zoneGateActive) return qsTr("正在解析视频分区...")
                if (DynamicsController.busy) return qsTr("正在查找匹配...")
                // 链式推进已无法继续(达轮上限或已到底)即呈现未找到匹配
                if (DynamicsController.ended || page.autoRounds >= page.autoRoundLimit) {
                    return qsTr("未找到匹配(已载 %1 条)").arg(String(page.pool.length))
                }
                return qsTr("正在查找匹配...")
            }
            textColor: FluTheme.fontSecondaryColor
        }

        ScrollBar.vertical: FluScrollBar {}
    }

    // ---- 空投影自动续链(轮间 400ms;不依赖滚动或布局事件) ----
    Timer {
        id: auto_timer

        interval: 400
        onTriggered: {
            if (page.items.length > 0 || DynamicsController.busy ||
                DynamicsController.ended || DynamicsController.zoneGateActive ||
                page.autoRounds >= page.autoRoundLimit) return
            page.autoRounds++
            DynamicsController.loadMore()
        }
    }

    // 链式推进入口:投影命中即复位;空投影且未到底才续链(重复 evaluate 借
    // restart 去重,轮计数只在真正发起 loadMore 时累加)
    function evaluateAutoChain() {
        if (page.items.length > 0) {
            page.autoRounds = 0
            auto_timer.stop()
            return
        }
        if (DynamicsController.zoneGateActive || DynamicsController.ended) {
            auto_timer.stop()
            return
        }
        if (DynamicsController.busy) return
        if (page.autoRounds >= page.autoRoundLimit) {
            auto_timer.stop()
            return
        }
        auto_timer.restart()
    }

    // 触底续载:最后三行阈值内(行高按 封面 169 + 信息区约 150 ≈ 320 估)自动请求
    // 下一页;用户手势续载同时复位自动轮计数(从保留 offset 恢复)
    function maybeLoadMore() {
        if (DynamicsController.busy || DynamicsController.ended) return
        var threshold = 3 * 320
        if (scroll_view.contentY + scroll_view.height >= scroll_view.contentHeight - threshold) {
            page.autoRounds = 0
            DynamicsController.loadMore()
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
        target: DynamicsController.cardModel
        function onRowsMoved() { Qt.callLater(masonry.relayout) }
        function onRowsInserted() { Qt.callLater(masonry.relayout) }
        function onRowsRemoved() { Qt.callLater(masonry.relayout) }
    }

    Connections {
        target: DynamicsController

        function onZoneNamesChanged() { page.syncZoneOptions() }

        function onItemsChanged() {
            page.lastError = ""
            page.evaluateAutoChain()
        }
        function onBusyChanged() {
            page.evaluateAutoChain()
        }
        function onEndedChanged() {
            page.evaluateAutoChain()
        }
        function onZoneGateActiveChanged() {
            page.evaluateAutoChain()
        }
        function onLoadFailed(message) {
            page.lastError = message
            info_bar.showError(message, 3000)
            page.evaluateAutoChain()
        }
    }

    Component.onCompleted: {
        syncZoneOptions()
        // 注入当前筛选档位(持久化值)与搜索词;首次进入拉首页,切页返回
        // (页面缓存 + Controller 单例)不重新拉取
        DynamicsController.categoryFilter = page.filter
        DynamicsController.zoneFilter = page.zoneFilter
        DynamicsController.searchText = page.queryLower
        if (!DynamicsController.busy && DynamicsController.pool.length === 0) {
            DynamicsController.refresh()
        }
    }
}
