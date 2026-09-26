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
// MainWindow 切页释放渲染,池/offset/筛选及滚动锚点由控制器保持。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):标题或 UP 主不分大小写子串,空词恢复全量。
    // 初始值读自控制器保留的搜索词(渲染重建后回显,而非清零覆盖);之后每次
    // 赋值即断开该绑定,变为普通可写属性(与 OnlineHistoryPage 同构)。
    property string searchQuery: DynamicsController.searchText
    readonly property string queryLower: searchQuery.trim().toLowerCase()

    property bool pageReady: false
    property bool resettingPosition: false
    property bool restoringPosition: true
    property var viewAnchor: DynamicsController.scrollAnchor

    function cardKey(item) {
        return item.category === "video" ? "video:" + String(item.aid) : "dynamic:" + String(item.id)
    }
    function rememberAnchor() {
        if (!pageReady || restoringPosition || resettingPosition || !items.length) return
        var index = grid.indexAt(grid.cellWidth / 2, grid.contentY + 1)
        if (index < 0) index = Math.max(0, Math.min(items.length - 1,
            Math.floor((grid.contentY - grid.originY) / grid.cellHeight) * content_area.gridColumns))
        var row = grid.itemAtIndex(index)
        var rowY = row ? row.y : grid.originY + Math.floor(index / content_area.gridColumns) * grid.cellHeight
        viewAnchor = {key: cardKey(items[index]), index: index,
                      fraction: (grid.contentY - rowY) / grid.cellHeight}
    }
    function restoreAnchor() {
        if (!pageReady) return
        restoringPosition = true
        grid.forceLayout()
        var anchor = viewAnchor
        if (items.length && anchor && anchor.key) {
            var index = -1
            for (var i = 0; i < items.length; ++i) {
                if (cardKey(items[i]) === anchor.key) { index = i; break }
            }
            // 锚点已被淘汰时回到现存最早条目,不拿旧索引跳进更老的下一页。
            if (index < 0) index = 0
            grid.positionViewAtIndex(index, GridView.Beginning)
            grid.forceLayout()
            var row = grid.itemAtIndex(index)
            var rowY = row ? row.y : grid.originY + Math.floor(index / content_area.gridColumns) * grid.cellHeight
            grid.contentY = Math.max(grid.originY, Math.min(rowY + anchor.fraction * grid.cellHeight,
                Math.max(grid.originY, grid.originY + grid.contentHeight - grid.height)))
        } else {
            grid.contentY = Math.max(grid.originY, Math.min(DynamicsController.scrollOffset,
                Math.max(grid.originY, grid.originY + grid.contentHeight - grid.height)))
        }
        if (restore_timer.running) return
        restoringPosition = false
        rememberAnchor()
        evaluateAutoChain()
    }
    function scheduleRestore() {
        if (!pageReady || resettingPosition) return
        restoringPosition = true
        restore_timer.restart()
    }
    Timer {
        id: restore_timer
        interval: 16
        onTriggered: page.restoreAnchor()
    }
    function resetPosition() {
        restore_timer.stop()
        restoringPosition = false
        viewAnchor = ({})
        DynamicsController.scrollAnchor = ({})
        DynamicsController.scrollOffset = 0
        grid.contentY = grid.originY
    }

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
    // 初始值读自控制器保留的分区选择(渲染重建后回显,而非清零覆盖)。
    property string zoneFilter: DynamicsController.zoneFilter

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

    // 不足一屏的投影自动续链:连续最多 5 轮(填满/刷新/手势续载复位)
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
        if (!pageReady) return
        AppPreferences.setValue("App.Dynamics.Filter", filter)
        if (filter !== "video" && zoneFilter !== "") zoneFilter = ""  // 离开视频档清空分区选择
        resettingPosition = true
        DynamicsController.categoryFilter = filter
        resetPosition()
        resettingPosition = false
        grid.measuredCardHeight = 0  // 内容形态可能大变,重新测算行高
        page.autoRounds = 0
        page.evaluateAutoChain()
    }
    onZoneFilterChanged: {
        if (!pageReady) return
        resettingPosition = true
        DynamicsController.zoneFilter = zoneFilter
        resetPosition()
        resettingPosition = false
        page.autoRounds = 0
        page.evaluateAutoChain()
    }
    onQueryLowerChanged: {
        if (!pageReady) return
        resettingPosition = true
        DynamicsController.searchText = page.searchQuery  // 原始输入,供渲染重建后回显
        resetPosition()
        resettingPosition = false
        page.autoRounds = 0
        page.evaluateAutoChain()
    }


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
                    page.resetPosition()  // 刷新后滚动位置回顶
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

    // ---- 内容区:虚拟化瀑布流网格(无分页栏) ----
    Item {
        id: content_area

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

        readonly property int gridColumns: Math.max(1, Math.floor(width / 316))
        readonly property real gridCardWidth: Math.min(300, width - 16)

        GridView {
            id: grid
            objectName: "dynamicsGrid"

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: content_area.gridColumns * cellWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: content_area.gridCardWidth + 16
            // 同一分类档内只增高(与 PopularPage 现有实现同构);切换分类档时
            // 内容形态差异较大(视频/专栏/图文等),重新测算避免残留过高空白。
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
            model: DynamicsController.cardModel
            cacheBuffer: cellHeight
            onContentYChanged: {
                page.rememberAnchor()
                if (moving || dragging) page.maybeLoadMore()
            }
            onMovementEnded: page.maybeLoadMore()
            onWidthChanged: page.scheduleRestore()
            onCellHeightChanged: page.scheduleRestore()
            onHeightChanged: Qt.callLater(page.evaluateAutoChain)

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

            ScrollBar.vertical: FluScrollBar {
                parent: grid.parent
                anchors {
                    top: parent.top
                    right: parent.right
                    bottom: parent.bottom
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
    }

    // 空或不足一屏的稀疏投影共用有限续链；失败后等待用户刷新/手势重试。
    Timer {
        id: auto_timer
        interval: 400
        onTriggered: {
            if (!page.needsMoreForViewport() || DynamicsController.busy ||
                DynamicsController.ended || DynamicsController.zoneGateActive ||
                page.lastError !== "" || page.autoRounds >= page.autoRoundLimit) return
            page.autoRounds++
            DynamicsController.loadMore()
        }
    }
    function needsMoreForViewport() {
        return page.items.length === 0 ||
            Math.ceil(page.items.length / content_area.gridColumns) * grid.cellHeight < grid.height
    }
    function evaluateAutoChain() {
        if (!pageReady || restoringPosition) return
        if (!needsMoreForViewport()) {
            page.autoRounds = 0
            auto_timer.stop()
            return
        }
        if (DynamicsController.zoneGateActive || DynamicsController.ended ||
            lastError !== "" || autoRounds >= autoRoundLimit) {
            auto_timer.stop()
            return
        }
        if (!DynamicsController.busy) auto_timer.restart()
    }
    function maybeLoadMore() {
        if (!pageReady || restoringPosition || DynamicsController.busy || DynamicsController.ended) return
        if (grid.contentY + grid.height >= grid.originY + grid.contentHeight - 3 * grid.cellHeight) {
            page.autoRounds = 0
            page.lastError = ""
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
        target: DynamicsController

        function onZoneNamesChanged() { page.syncZoneOptions() }

        function onItemsAboutToChange() {
            page.rememberAnchor()
            if (!page.resettingPosition) page.restoringPosition = true
        }
        function onItemsChanged() {
            page.scheduleRestore()
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
        // 注入当前筛选档位(持久化值);zoneFilter/searchQuery 已在属性初始化时
        // 从控制器回显(见上方声明),此处不再覆盖写入,避免渲染重建时用页面
        // 重置后的默认值抹掉控制器保留的分区/搜索状态。首次进入拉首页,
        // 切页返回(数据由 Controller 单例保留)不重新拉取。
        DynamicsController.categoryFilter = page.filter
        pageReady = true
        scheduleRestore()
        if (!DynamicsController.busy && DynamicsController.pool.length === 0) {
            DynamicsController.refresh()
        }
    }

    // 渲染即将释放(页面切走)前记忆滚动位置,供下次重建后首次布局读回
    Component.onDestruction: {
        rememberAnchor()
        DynamicsController.scrollAnchor = viewAnchor
        DynamicsController.scrollOffset = grid.contentY
    }
}
