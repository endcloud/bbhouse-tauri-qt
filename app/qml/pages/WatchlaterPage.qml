import QtQuick
import QtQuick.Controls
import FluentUI
import bbhouse

// 稍后再看页(watchlater-ui):云端单次全量拉取,内存搜索后每页显示 30 条。
// 翻页和滚动到底均不触发请求(ToviewApi 无翻页参数),不落库。
// 失效稿件(state<0)标题替换为"已失效"占位且禁播;
// PGC 条目(番剧/影视)经条目自带 epid+cid 直起播;刷新期间
// 保留既有卡片并显示加载指示,新数据到达整体替换;已看完(负进度)不显进度。
// 搜索:页面独立 searchQuery 提交(全量池投影,零网络零续拉);投影为空
// 直接呈现"未找到匹配(已载 N 条)",无自动续链语义。页面与池(Watchlater-
// Controller 单例)均跨页缓存,切页再返回不重新拉取。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    // 标题栏搜索投影(由 MainWindow 向当前页提交):标题或 UP 主不分大小写子串,空词恢复全量。
    // 初始值读自控制器保留的搜索词(渲染重建后回显,而非清零覆盖)。
    property string searchQuery: WatchlaterController.searchText
    property bool stateReady: false
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    readonly property bool searching: queryLower !== ""

    onSearchQueryChanged: if (stateReady) WatchlaterController.searchText = searchQuery

    // 全量池(绑定 Controller 单例;失效占位等展示态修饰在此完成)
    readonly property var rawPool: WatchlaterController.pool
    readonly property var pool: decorate(rawPool)
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
    readonly property int pageSize: 30
    // 初始值读自控制器保留的页码(渲染重建后回显,而非硬编码 1)
    property int pageIndex: WatchlaterController.pageIndex
    readonly property int totalPages: Math.max(1, Math.ceil(filteredItems.length / pageSize))
    readonly property var pageItems: filteredItems.slice((pageIndex - 1) * pageSize,
                                                        pageIndex * pageSize)

    function selectPage(requestedPage) {
        pageIndex = Math.max(1, Math.min(requestedPage, totalPages))
        WatchlaterController.pageIndex = pageIndex
        pagination_bar.pageCurrent = pageIndex
        scroll_anim.stop()
        scroll_view.contentY = 0
    }

    // 搜索覆盖全量池并回首页;刷新后仅在原页码越界时回到新的末页。
    onQueryLowerChanged: if (stateReady) selectPage(1)
    onTotalPagesChanged: {
        if (stateReady && pageIndex > totalPages) selectPage(totalPages)
    }

    // 最近一次失败文案(空串 = 无);刷新失败保留旧卡片,状态条与 InfoBar 提示
    property string lastError: ""

    // 失效稿件展示态:标题替换为本地化占位(搜索与卡片均消费修饰后数据)
    function decorate(source) {
        var result = []
        result.length = source.length
        for (var i = 0; i < source.length; i++) {
            var entry = source[i]
            if (entry.invalid === true) {
                entry = Object.assign({}, entry, {title: qsTr("已失效")})
            }
            result[i] = entry
        }
        return result
    }

    // ---- 瀑布流列参数(与本地/在线历史页同款):列宽 300、步距 316、整体居中 ----
    readonly property int columnStride: 316
    readonly property int columnGap: 16
    readonly property int viewportWidth: width - 48
    readonly property int columnCount: Math.max(1, Math.floor((viewportWidth + columnGap) / columnStride))
    readonly property int gridWidth: Math.max(0, columnCount * columnStride - columnGap)
    // 瀑布流内容区右缘到页面右缘的距离(刷新按钮对齐用的宽度同步口径)
    readonly property int gridRightInset:
        24 + Math.ceil(Math.max(0, viewportWidth - gridWidth) / 2)
    // 列数变化(窗口宽度变化)时重排瀑布流(columnCount 定义在页级,masonry 内
    // 无同名属性,故重排触发挂在这里)
    onColumnCountChanged: masonry.relayout()

    FluInfoBar {
        id: info_bar

        root: page
    }

    // 冻结标题带(高 56 / 边距 24 / 垂直居中,统一规格;带内无筛选控件)
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
            text: qsTr("稍后再看")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
        }
    }

    // 工具栏:仅图标刷新按钮(右缘与瀑布流内容区右缘对齐)+ 刷新期间加载指示
    Row {
        id: toolbar

        spacing: 10
        anchors {
            top: title_band.bottom
            topMargin: 4
            right: parent.right
            rightMargin: page.gridRightInset
        }
        // 刷新进行中的加载指示器(既有卡片保持可见,不闪空白)
        FluProgressRing {
            id: refresh_ring

            indeterminate: true
            strokeWidth: 3
            width: 22
            height: 22
            visible: WatchlaterController.busy
            anchors.verticalCenter: parent.verticalCenter
        }
        FluIconButton {
            id: btn_refresh

            width: 34
            height: 34
            iconSource: FluentIcons.Refresh
            iconSize: 14
            enabled: !WatchlaterController.busy
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                page.lastError = ""
                WatchlaterController.refresh()
            }
        }
    }

    // ---- 内容区:瀑布流滚动容器(分页栏固定底部,滚动到底无续载) ----
    Flickable {
        id: scroll_view

        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: masonry.y + masonry.height + 12
        anchors {
            top: toolbar.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            bottom: pagination_host.top
            bottomMargin: 8
            leftMargin: 24
            rightMargin: 24
        }

        // 瀑布流:最短列放置自绘(列宽 300 定宽、整体居中、无横向滚动),
        // 与本地/在线历史页同构
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

            Repeater {
                id: cards_repeater

                onCountChanged: Qt.callLater(masonry.relayout)

                model: page.pageItems

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

        // 列表底部状态条:刷新失败提示(既有卡片保留,可重试)
        FluText {
            visible: !WatchlaterController.busy && WatchlaterController.loaded &&
                     page.lastError !== ""
            text: qsTr("刷新失败,点击右上角刷新重试")
            textColor: FluTheme.fontSecondaryColor
            anchors {
                horizontalCenter: parent.horizontalCenter
                top: masonry.bottom
                topMargin: 8
            }
        }

        ScrollBar.vertical: FluScrollBar {}
    }

    // 空态与滚动视口同级，居中依据可见区域，不受内容高度和滚动偏移影响。
    FluText {
        anchors.centerIn: scroll_view
        width: scroll_view.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: page.filteredItems.length === 0 && page.pool.length === 0
        text: {
            if (WatchlaterController.busy) return qsTr("正在加载稍后再看列表...")
            if (page.lastError !== "") {
                return WatchlaterController.unauthorized
                        ? qsTr("登录失效,点击右上角刷新重试")
                        : qsTr("加载失败,点击右上角刷新重试")
            }
            return qsTr("暂无稍后再看内容")
        }
        textColor: FluTheme.fontSecondaryColor
    }
    FluText {
        anchors.centerIn: scroll_view
        width: scroll_view.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: page.filteredItems.length === 0 && page.pool.length > 0 && page.searching
        text: qsTr("未找到匹配(已载 %1 条)").arg(String(page.pool.length))
        textColor: FluTheme.fontSecondaryColor
    }

    // 内存分页:搜索匹配总数决定页数,不调用 Controller 或追加网络请求。
    Column {
        id: pagination_host

        width: parent.width - 48
        spacing: 4
        height: visible ? implicitHeight : 0
        visible: page.filteredItems.length > 0
        anchors {
            bottom: parent.bottom
            bottomMargin: 12
            horizontalCenter: parent.horizontalCenter
        }
        FluText {
            text: qsTr("第 %1 / %2 页 · 显示 %3-%4 共 %5 条")
                    .arg(String(page.pageIndex)).arg(String(page.totalPages))
                    .arg(String((page.pageIndex - 1) * page.pageSize + 1))
                    .arg(String(Math.min(page.pageIndex * page.pageSize, page.filteredItems.length)))
                    .arg(String(page.filteredItems.length))
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        FluPagination {
            id: pagination_bar

            width: Math.min(implicitWidth, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            pageButtonCount: page.width < 720 ? 3 : 5
            objectName: "watchlaterPagination"
            pageCurrent: page.pageIndex
            itemCount: page.filteredItems.length
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
                page.selectPage(requestedPage)
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
        target: WatchlaterController

        function onPoolChanged() {
            page.lastError = ""
        }
        function onLoadFailed(message) {
            page.lastError = message
            info_bar.showError(message, 3000)
        }
    }

    Component.onCompleted: {
        stateReady = true
        selectPage(pageIndex)
        // 页面首次创建时装载全量;切页返回(页面缓存)不重新拉取
        if (!WatchlaterController.loaded && !WatchlaterController.busy) {
            WatchlaterController.refresh()
        }
    }
}
