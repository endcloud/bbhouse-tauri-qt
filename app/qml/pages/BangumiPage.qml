import QtQuick
import QtQuick.Controls
import FluentUI
import bbhouse

// 追番追剧页(bangumi-ui):两桶(type=1 追番 / type=2 追剧)标准服务端分页 30/页,
// 常规四档 chips(番剧/国创/影视/纪录片)= 对当前桶页的 season_type 客户端投影 ——
// 番剧=1、国创=4 同享桶 1 页码;影视=2/5、纪录片=3 共享桶 2 页码;同桶切档零网络。
// 等宽竖版网格(SeasonCard 170 定宽,行内均分居中,稀疏行整体居中)+ 分页栏 +
// 回顶 FAB。卡片左键 → 拉剧集详情起播(剧集模式,从云端最近观看分集续播);
// 右键 → 详情浮层(evaluate + 播放全集 + 单集列表)。搜索:页面独立 searchQuery
// 注入，常规档仅投影当前页；港澳台从全部已追番筛选后搜索并分页。页面与桶页码(Controller 单例)
// 跨页缓存,切页再返回不重新拉取。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):当前档当前页标题不分大小写子串,空词恢复全量
    property string searchQuery: ""
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    readonly property bool searching: queryLower !== ""

    onSearchQueryChanged: BangumiController.setRegionalSearch(searchQuery)

    // ---- 五档定义(桶内 season_type 混合下发,档位是客户端过滤) ----
    readonly property var tabDefs: [
        { key: "anime", label: qsTr("番剧"), types: [1], bucket: 1 },
        { key: "guochuang", label: qsTr("国创"), types: [4], bucket: 1 },
        { key: "movie", label: qsTr("影视"), types: [2, 5], bucket: 2 },
        { key: "documentary", label: qsTr("纪录片"), types: [3], bucket: 2 },
        { key: "regional", label: qsTr("港澳台"), types: [], bucket: 3 }
    ]
    property string currentTab: "anime"
    readonly property var activeTab: tabByKey(currentTab)

    readonly property int pageSize: 30
    // 最近一次失败文案(空串 = 无);失败保留旧卡片,状态条与 InfoBar 提示
    property string lastError: ""

    // 点击起播与详情浮层各自的在途详情请求(后点取代先点)
    property double pendingPlaySeasonId: 0
    property double pendingDetailSeasonId: 0

    function tabByKey(key) {
        for (var i = 0; i < tabDefs.length; i++) {
            if (tabDefs[i].key === key) return tabDefs[i]
        }
        return tabDefs[0]
    }

    // ---- 投影:桶页 → 档位过滤 → 搜索标题过滤(全部纯内存,零网络) ----
    readonly property var pageItems: BangumiController.pageItems
    readonly property var tabItems: {
        var types = activeTab.types
        var result = []
        for (var i = 0; i < pageItems.length; i++) {
            if (currentTab === "regional" ? pageItems[i].regional
                    : (!pageItems[i].regional && types.indexOf(pageItems[i].seasonType) !== -1)) {
                result.push(pageItems[i])
            }
        }
        return result
    }
    readonly property var filteredItems: {
        var q = queryLower
        var source = tabItems
        // 港澳台已在 Controller 中对全量结果搜索，避免二次按页过滤。
        if (q === "" || currentTab === "regional") return source
        var result = []
        for (var i = 0; i < source.length; i++) {
            var title = String(source[i].title || "").toLowerCase()
            if (title.indexOf(q) !== -1) {
                result.push(source[i])
            }
        }
        return result
    }

    // ---- 等宽网格列参数:卡片 170 定宽、步距 186、行内均分居中(稀疏行也居中) ----
    readonly property int viewportWidth: width - 48
    readonly property int gridRightInset:
        24 + Math.ceil(Math.max(0, viewportWidth - (grid.columnCount * 186 - 16)) / 2)
    readonly property int bandRightInset: Math.max(0, gridRightInset - 24)

    function selectTab(key) {
        if (currentTab === key) return
        currentTab = key
        // 同桶切档纯重投影零网络;换桶未装载由 Controller 拉该桶第 1 页
        BangumiController.setBucket(activeTab.bucket)
        scroll_view.contentY = 0  // 切档回顶(规约)
    }

    function lastEpStartIndex(detail) {
        // 起播分集定位:云端最近观看分集(last_ep_id),无观看记录第 1 集
        if (!detail || !(detail.lastEpId > 0)) return 0
        var episodes = detail.episodes || []
        for (var i = 0; i < episodes.length; i++) {
            if (episodes[i].epId === detail.lastEpId) return i
        }
        return 0
    }

    function playFromDetail(detail, startIndex) {
        if (!detail || !detail.episodes || detail.episodes.length === 0) return
        PlayerController.playSeason(
                    { title: detail.title, coverUrl: detail.coverUrl, regional: Boolean(detail.regional),
                      lastEpId: detail.lastEpId,
                      lastTimeSeconds: detail.lastTimeSeconds },
                    detail.episodes, startIndex)
        FluRouter.navigate("/player")
    }

    // 卡片左键:直接起播该节目(规约:点击卡片起播剧集,不开浏览器)
    function playSeason(item) {
        if (!item || !(item.seasonId > 0)) return
        pendingPlaySeasonId = 0
        pendingDetailSeasonId = 0
        var cached = BangumiController.seasonDetail(item.seasonId, Boolean(item.regional))
        if (cached && cached.seasonId > 0) {
            playFromDetail(cached, lastEpStartIndex(cached))
            return
        }
        pendingPlaySeasonId = item.seasonId
        info_bar.showInfo(qsTr("正在获取剧集信息..."), 3000)
    }

    // 卡片右键:详情浮层(evaluate + 播放全集 + 单集列表)
    function openDetailDialog(item) {
        if (!item || !(item.seasonId > 0)) return
        // 简介与副标来自条目自身(follow list;season 端点不含这些字段)
        var progress = String(item.progressText || "").trim()
        var evaluate = String(item.evaluate || "").trim()
        season_dialog.subtitleLine = progress !== "" ? progress : String(item.subtitle || "").trim()
        season_dialog.evaluateText = evaluate
        pendingPlaySeasonId = 0
        pendingDetailSeasonId = 0
        var cached = BangumiController.seasonDetail(item.seasonId, Boolean(item.regional))
        if (cached && cached.seasonId > 0) {
            season_dialog.title = String(cached.title || item.title || "")
            season_dialog.detail = cached
            season_dialog.open()
            return
        }
        pendingDetailSeasonId = item.seasonId
        season_dialog.title = String(item.title || "")
        season_dialog.detail = null
        info_bar.showInfo(qsTr("正在获取剧集信息..."), 3000)
    }

    FluInfoBar {
        id: info_bar

        root: page
    }

    // 冻结标题带(高 56 / 边距 24 / 垂直居中,统一规格):大标题 + 四档 chips +
    // 右缘对齐刷新按钮(宽度同步机制)
    Item {
        id: title_band

        readonly property bool compact: title_text.implicitWidth + tab_row.width + toolbar.width + page.bandRightInset + 32 > width
        height: compact ? 96 : 56
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        FluText {
            id: title_text

            text: qsTr("番剧")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: title_band.compact ? -20 : 0
            }
        }
        Flickable {
            id: tab_strip
            height: 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: tab_row.width
            contentHeight: height

            anchors {
                left: title_band.compact ? parent.left : title_text.right
                leftMargin: title_band.compact ? 0 : 16
                right: title_band.compact ? parent.right : toolbar.left
                rightMargin: title_band.compact ? 0 : 12
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: title_band.compact ? 22 : 0
            }
            Row {
                id: tab_row
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: page.tabDefs

                    delegate: FluToggleButton {
                        required property var modelData

                        text: modelData.label
                        checked: page.currentTab === modelData.key
                        clickListener: function () {
                            page.selectTab(modelData.key)
                        }
                    }
                }
            }
        }
        Row {
            id: toolbar

            spacing: 10
            anchors {
                right: parent.right
                rightMargin: page.bandRightInset
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: title_band.compact ? -20 : 0
            }
            // 刷新进行中的加载指示器(既有卡片保持可见,不闪空白)
            FluProgressRing {
                id: refresh_ring

                indeterminate: true
                strokeWidth: 3
                width: 22
                height: 22
                visible: BangumiController.busy
                anchors.verticalCenter: parent.verticalCenter
            }
            FluIconButton {
                id: btn_refresh

                width: 34
                height: 34
                iconSource: FluentIcons.Refresh
                iconSize: 14
                enabled: !BangumiController.busy
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    page.lastError = ""
                    BangumiController.refresh()
                }
            }
        }
    }

    // ---- 内容区:等宽网格滚动容器(仅竖向滚动,分页栏固定底部) ----
    Flickable {
        id: scroll_view

        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: grid.y + grid.height + 12
        anchors {
            top: title_band.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            bottom: pagination_host.top
            bottomMargin: 8
            leftMargin: 24
            rightMargin: 24
        }

        // 等宽网格:列数 = floor(可用宽/186),行内均分,容器整体居中 ——
        // 稀疏行(未排满)随容器居中,不贴左
        Item {
            id: grid

            width: parent.width
            height: Math.max(contentHeight, 1)
            x: Math.max(0, Math.floor((width - gridWidth) / 2))
            readonly property int cardWidth: 170
            readonly property int columnGap: 16
            readonly property int stride: cardWidth + columnGap
            readonly property int columnCount: Math.max(1, Math.floor((width + columnGap) / stride))
            readonly property int gridWidth: Math.max(0, columnCount * stride - columnGap)
            readonly property int rowGap: 16
            property real contentHeight: 0

            function relayout() {
                var count = cards_repeater.count
                if (count === 0) {
                    grid.contentHeight = 0
                    return
                }
                var y = 0
                var rowHeight = 0
                for (var i = 0; i < count; i++) {
                    var card = cards_repeater.itemAt(i)
                    if (!card) continue
                    var col = i % grid.columnCount
                    if (col === 0 && i > 0) {
                        y += rowHeight + grid.rowGap
                        rowHeight = 0
                    }
                    card.x = col * grid.stride
                    card.y = y
                    rowHeight = Math.max(rowHeight, card.height)
                }
                grid.contentHeight = y + rowHeight
            }

            onColumnCountChanged: relayout()
            onWidthChanged: relayout()

            Repeater {
                id: cards_repeater

                onCountChanged: Qt.callLater(grid.relayout)

                model: page.filteredItems

                delegate: SeasonCard {
                    id: card

                    cardItem: modelData
                    width: grid.cardWidth
                    height: implicitHeight
                    onImplicitHeightChanged: Qt.callLater(grid.relayout)
                    Component.onCompleted: Qt.callLater(grid.relayout)
                    onCardClicked: function (cardItem) {
                        page.playSeason(cardItem)
                    }
                    onDetailRequested: function (cardItem) {
                        page.openDetailDialog(cardItem)
                    }
                }
            }
        }

        // 网格底部状态条:刷新失败提示(既有卡片保留,可重试)
        FluText {
            visible: !BangumiController.busy && page.lastError !== "" &&
                     page.pageItems.length > 0
            text: qsTr("刷新失败,点击右上角刷新重试")
            textColor: FluTheme.fontSecondaryColor
            anchors {
                horizontalCenter: parent.horizontalCenter
                top: grid.bottom
                topMargin: 8
            }
        }

        // 空态:桶页未装载/失败(可重试)vs 搜索无匹配 vs 档位过滤后为空/空桶
        FluText {
            anchors.centerIn: parent
            visible: page.filteredItems.length === 0 && page.pageItems.length === 0
            text: {
                if (BangumiController.busy) return qsTr("正在加载追番追剧列表...")
                if (page.lastError !== "") {
                    return BangumiController.unauthorized
                            ? qsTr("登录失效,点击右上角刷新重试")
                            : qsTr("加载失败,点击右上角刷新重试")
                }
                if (page.currentTab === "regional" && page.searching) return qsTr("未找到匹配的港澳台番剧")
                return qsTr("暂无记录")
            }
            textColor: FluTheme.fontSecondaryColor
        }
        FluText {
            anchors.centerIn: parent
            visible: page.filteredItems.length === 0 && page.pageItems.length > 0
            text: page.searching ? qsTr("未找到匹配(已载 %1 条)").arg(String(page.tabItems.length))
                                 : qsTr("暂无记录")
            textColor: FluTheme.fontSecondaryColor
        }

        ScrollBar.vertical: FluScrollBar {}
    }

    // ---- 分页栏:首页/上一页/数字/下一页/末页 + 口径文案(加载中双向禁用) ----
    Column {
        id: pagination_host

        width: parent.width - 48
        spacing: 4
        height: visible ? implicitHeight : 0
        enabled: !BangumiController.busy
        opacity: enabled ? 1 : 0.5
        anchors {
            bottom: parent.bottom
            bottomMargin: 12
            horizontalCenter: parent.horizontalCenter
        }
        FluText {
            function indicator() {
                // 分页信息:总数为 0(空桶/档位过滤后无条目)呈现"暂无记录"
                var n = BangumiController.currentTotal
                if (n <= 0) return qsTr("暂无记录")
                var totalPages = Math.max(1, Math.ceil(n / page.pageSize))
                var current = Math.max(1, BangumiController.currentPage)
                var from = (current - 1) * page.pageSize + 1
                var to = Math.min(current * page.pageSize, n)
                return qsTr("第 %1 / %2 页 · 显示 %3-%4 共 %5 条")
                        .arg(String(current)).arg(String(totalPages))
                        .arg(String(from)).arg(String(to)).arg(String(n))
            }
            text: indicator()
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        FluPagination {
            id: pagination_bar

            width: Math.min(implicitWidth, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            pageButtonCount: page.width < 720 ? 3 : 5

            itemCount: BangumiController.currentTotal
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
                BangumiController.loadPage(requestedPage)
            }
        }
    }

    // ---- 回到顶部 FAB(圆形;下滚超过一屏出现;浮层覆盖其上) ----
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

    // ---- 详情浮层:副标 + evaluate + 播放全集 + 单集列表 ----
    FluContentDialog {
        id: season_dialog

        property var detail: null
        // 简介与副标来自条目自身(打开浮层时注入;season 端点不含这些字段)
        property string subtitleLine: ""
        property string evaluateText: ""

        implicitWidth: 480
        buttonFlags: FluContentDialogType.NegativeButton
        negativeText: qsTr("关闭")
        contentDelegate: Component {
            Item {
                implicitHeight: detail_column.height + 8

                Column {
                    id: detail_column

                    width: parent.width
                    spacing: 8
                    // 副标:观看进度文本优先,否则条目副标(如"更新至第 12 集")
                    FluText {
                        width: parent.width
                        visible: season_dialog.subtitleLine !== ""
                        text: season_dialog.subtitleLine
                        font: FluTextStyle.Caption
                        textColor: FluTheme.fontSecondaryColor
                        wrapMode: Text.NoWrap
                        maximumLineCount: 1
                        elide: Text.ElideRight
                    }
                    // 简介 evaluate:超长内部滚动
                    Flickable {
                        width: parent.width
                        height: Math.min(evaluate_text.height, 80)
                        contentWidth: width
                        contentHeight: evaluate_text.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: FluScrollBar {}
                        FluText {
                            id: evaluate_text

                            width: parent.width
                            visible: season_dialog.evaluateText !== ""
                            text: season_dialog.evaluateText
                            font: FluTextStyle.Caption
                            textColor: FluTheme.fontSecondaryColor
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Row {
                        spacing: 12
                        FluFilledButton {
                            text: qsTr("播放全集")
                            enabled: season_dialog.detail &&
                                     season_dialog.detail.episodes &&
                                     season_dialog.detail.episodes.length > 0
                            onClicked: {
                                var detail = season_dialog.detail
                                season_dialog.close()
                                page.playFromDetail(detail, page.lastEpStartIndex(detail))
                            }
                        }
                        FluText {
                            visible: season_dialog.detail && season_dialog.detail.episodes
                            text: season_dialog.detail && season_dialog.detail.episodes
                                  ? qsTr("共 %1 集").arg(String(season_dialog.detail.episodes.length))
                                  : ""
                            textColor: FluTheme.fontSecondaryColor
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    // 单集列表(点击单集起播;虚拟化,长列表内部滚动)
                    ListView {
                        id: episode_list

                        width: parent.width
                        height: 220
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: season_dialog.detail ? (season_dialog.detail.episodes || []) : []
                        delegate: Item {
                            id: episode_item

                            required property int index
                            required property var modelData

                            width: episode_list.width
                            height: 34
                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                color: episode_mouse.containsMouse ? FluTheme.itemHoverColor
                                                                   : Qt.rgba(0, 0, 0, 0)
                            }
                            Row {
                                spacing: 8
                                anchors {
                                    left: parent.left
                                    leftMargin: 4
                                    right: parent.right
                                    rightMargin: 4
                                    verticalCenter: parent.verticalCenter
                                }
                                FluText {
                                    text: String(episode_item.index + 1)
                                    font: FluTextStyle.Caption
                                    opacity: 0.7
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                FluText {
                                    width: parent.width - 24
                                    text: {
                                        var episode = episode_item.modelData
                                        var title = String(episode.longTitle || "")
                                        if (title === "") title = String(episode.title || "")
                                        var badge = String(episode.badge || "")
                                        return badge !== "" ? title + "  [" + badge + "]" : title
                                    }
                                    font.pixelSize: 13
                                    wrapMode: Text.NoWrap
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: episode_mouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    season_dialog.close()
                                    page.playFromDetail(season_dialog.detail, episode_item.index)
                                }
                            }
                        }
                        FluScrollBar {
                            anchors {
                                right: parent.right
                                top: parent.top
                                bottom: parent.bottom
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: BangumiController

        function onPageItemsChanged() {
            page.lastError = ""
        }
        function onPageInfoChanged() {
            pagination_bar.pageCurrent = Math.max(1, BangumiController.currentPage)
        }
        function onLoadFailed(message) {
            page.lastError = message
            info_bar.showError(message, 3000)
        }
        function onSeasonDetailReady(detail) {
            if (page.pendingPlaySeasonId > 0 && detail.seasonId === page.pendingPlaySeasonId) {
                page.pendingPlaySeasonId = 0
                page.playFromDetail(detail, page.lastEpStartIndex(detail))
            } else if (page.pendingDetailSeasonId > 0 &&
                       detail.seasonId === page.pendingDetailSeasonId) {
                page.pendingDetailSeasonId = 0
                season_dialog.title = String(detail.title || season_dialog.title)
                season_dialog.detail = detail
                season_dialog.open()
            }
        }
        function onErrorOccurred(message) {
            // 剧集详情拉取失败:仅提示,不影响页面浏览(规约)
            page.pendingPlaySeasonId = 0
            page.pendingDetailSeasonId = 0
            info_bar.showError(message, 3000)
        }
    }

    Component.onCompleted: {
        // 页面首次创建时装载当前桶(默认桶 1 第 1 页);切页返回(页面缓存 +
        // Controller 单例页码记忆)不重新拉取
        BangumiController.setRegionalSearch(page.searchQuery)
        BangumiController.ensureCurrentBucketLoaded()
    }
}
