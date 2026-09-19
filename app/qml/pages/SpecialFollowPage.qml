import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import bbhouse
import "../js/Format.js" as Format

// 特别关注页(special-follow-ui v3):本地 UP 列表(快照 + 一次性种子导入)+
// 横向头像条(仅圆形头像,无文字;选中态底色胶囊 = Acrylic 互补材质的等价实现,
// 与主视图容器卡片底视觉连续)+ 单 SelectorBar 三档(投稿/合集/专栏)。
// 投稿:ArcSearchApi 服务端 30/页 + 分区行(响应 tlist 索引,服务端 tid 过滤)+
// 排序三档;合集:SeasonsApi 服务端 20/页(端点硬上限)+ 卡片夹 widget +
// 点击展开拉全视频("播放全部"走 PlayerController.playRange 批量入列);
// 专栏:ArticleApi 一次全量,客户端 30/页切片。搜索:页面独立 searchQuery 提交,
// 对当前页已载条目做纯内存投影(零网络零翻页)。会话状态在 Controller 单例,
// 切页返回不重拉;三档已载内容/筛选/排序/页码按 UP 独立缓存。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0
    property var controller: SpecialFollowController
    property bool spaceMode: false

    // 标题栏搜索投影(由 MainWindow 向当前页提交):当前档当前页条目,空词恢复全量
    property string searchQuery: ""
    readonly property string queryLower: searchQuery.trim().toLowerCase()
    readonly property bool searching: queryLower !== ""

    // ---- 三档定义(单 SelectorBar) ----
    readonly property var tabDefs: [
        { key: "arc", label: qsTr("投稿") },
        { key: "seasons", label: qsTr("合集") },
        { key: "articles", label: qsTr("专栏") }
    ]
    property int currentTab: 0

    // ---- UP 列表与当前选中 ----
    readonly property bool upsReady: spaceMode || page.controller.upsReady
    readonly property var ups: page.controller.ups
    readonly property bool hasUps: spaceMode ? currentMid > 0 : ups.length > 0
    readonly property double currentMid: page.controller.currentMid
    readonly property var currentUp: page.spaceMode ? ({}) : upForMid(currentMid)
    readonly property string currentUpName: String(currentUp.name || "").trim()

    // 每档最近一次失败文案(空串 = 无;失败保留旧卡,状态条与 InfoBar 提示)
    property string arcError: ""
    property string seasonError: ""
    property string articleError: ""

    // ---- 合集展开态(点击卡片夹进入该合集视频列表) ----
    property var expandedSeason: null
    property var expandedVideos: []
    property int pendingSeasonRequest: 0
    property int pendingSeasonAction: 0  // 1 = 播放全部 2 = 展开
    property int seasonRequestCounter: 0

    // ---- 管理弹层勾选镜像(mid → bool;控制器为最终事实,QML 镜像驱动绑定) ----
    property var checkedMap: ({})

    // ---- 投影:当前档当前页 → 搜索词过滤(全部纯内存,零网络零翻页) ----
    readonly property var arcFiltered: {
        var q = queryLower
        var source = page.controller.arcItems
        if (q === "") return source
        var result = []
        for (var i = 0; i < source.length; i++) {
            var title = String(source[i].title || "").toLowerCase()
            var author = String(page.cardForDisplay(source[i]).authorName || "").toLowerCase()
            if (title.indexOf(q) !== -1 || author.indexOf(q) !== -1) {
                result.push(source[i])
            }
        }
        return result
    }
    // 合集档:未展开投影列表(按名称),展开后投影该合集已载视频(按标题)
    readonly property var seasonsViewItems: {
        var q = queryLower
        var source = page.expandedSeason ? page.expandedVideos
                                         : page.controller.seasonItems
        if (q === "") return source
        var result = []
        for (var i = 0; i < source.length; i++) {
            var title = String(source[i].title || "").toLowerCase()
            if (title.indexOf(q) !== -1) result.push(source[i])
        }
        return result
    }
    readonly property int articlePageSize: 30
    // 专栏:全量本地切片(30/页)后投影(按名称)
    readonly property var articleSlice: {
        var all = page.controller.articleItems
        var from = (Math.max(1, page.controller.articlePage) - 1) * articlePageSize
        var result = []
        for (var i = from; i < all.length && result.length < articlePageSize; i++) {
            result.push(all[i])
        }
        return result
    }
    readonly property var articleFiltered: {
        var q = queryLower
        var source = articleSlice
        if (q === "") return source
        var result = []
        for (var i = 0; i < source.length; i++) {
            var title = String(source[i].title || "").toLowerCase()
            if (title.indexOf(q) !== -1) result.push(source[i])
        }
        return result
    }

    // ---- 分页口径(投稿/专栏 30,合集 20) ----
    readonly property int pageSize: currentTab === 1 ? 20 : 30
    readonly property int pageInfoPage: currentTab === 0 ? page.controller.arcPage
                                  : currentTab === 1 ? page.controller.seasonPage
                                  : page.controller.articlePage
    readonly property int pageInfoTotal: currentTab === 0 ? page.controller.arcTotal
                                   : currentTab === 1 ? page.controller.seasonTotal
                                   : page.controller.articleTotal
    readonly property bool tabBusy: currentTab === 0 ? page.controller.arcBusy
                                  : currentTab === 1 ? page.controller.seasonBusy
                                  : page.controller.articleBusy

    // 宽度同步机制:刷新按钮右缘与投稿瀑布流右缘对齐(与既有页面同口径;
    // 主容器内边距 16×2 + 页面边距 24×2)
    readonly property int arcMasonryWidth: width - 48 - 32
    readonly property int arcStride: 316
    readonly property int arcColumnCount:
        Math.max(1, Math.floor((arcMasonryWidth + 16) / arcStride))
    readonly property int bandRightInset:
        Math.max(0, Math.ceil(Math.max(0, arcMasonryWidth -
                            (arcColumnCount * arcStride - 16)) / 2))

    function resetView() {
        searchQuery = ""
        currentTab = 0
        expandedSeason = null
        expandedVideos = []
        pendingSeasonRequest = 0
        pendingSeasonAction = 0
        arcError = ""
        seasonError = ""
        articleError = ""
        scroll_view.contentY = 0
        syncPaginationBar()
    }

    function selectTab(index) {
        if (currentTab === index) return
        currentTab = index
        scroll_view.contentY = 0  // 切档回顶(数据状态仍按档缓存)
        syncPaginationBar()
        page.controller.ensureCurrentTabLoaded(index)
    }

    function refreshCurrentTab() {
        if (currentTab === 0) page.controller.refreshArc()
        else if (currentTab === 1) page.controller.refreshSeasons()
        else page.controller.refreshArticles()
    }

    function selectUp(mid) {
        if (mid === currentMid) return
        page.controller.selectUp(mid)
    }

    function syncPaginationBar() {
        pagination_bar.pageCurrent = Math.max(1, pageInfoPage)
    }

    function upForMid(mid) {
        var key = String(mid || "")
        for (var i = 0; i < ups.length; i++) {
            if (String(ups[i].mid) === key) return ups[i]
        }
        return ({})
    }

    function cardForDisplay(item) {
        var result = Object.assign({}, item)
        if (spaceMode) {
            result.authorMid = page.controller.profileMid
            result.authorName = page.controller.profileName
            result.faceUrl = page.controller.profileFace
        } else {
            // 按条目的作者身份补资料，避免快速切换 UP 时误用当前昵称。
            var authorMid = String(item.authorMid || currentMid)
            var author = upForMid(authorMid)
            result.authorMid = authorMid
            if (String(item.authorName || "").trim() === "")
                result.authorName = String(author.name || "")
            if (!item.faceUrl) result.faceUrl = String(author.faceUrl || "")
        }
        return result
    }

    function formatNumber(value) {
        return (Number(value) || 0).toLocaleString()
    }

    // 合集卡:展开该合集视频列表(拉全,有加载指示)
    function openSeason(item) {
        if (!item || !(item.id > 0)) return
        expandedSeason = item
        expandedVideos = []
        scroll_view.contentY = 0
        requestSeasonVideos(item, 2)
    }

    // 卡片夹播放按钮:拉全 → 批量入列 → 自首集起播(不触发卡片跳转)
    function playSeasonAll(item) {
        if (!item || !(item.id > 0)) return
        requestSeasonVideos(item, 1)
    }

    function requestSeasonVideos(item, action) {
        if (page.controller.seasonVideosBusy) {
            info_bar.showInfo(qsTr("正在获取合集视频,请稍候..."), 2500)
            return
        }
        var request = ++page.seasonRequestCounter
        pendingSeasonRequest = request
        pendingSeasonAction = action
        if (action === 1) info_bar.showInfo(qsTr("正在获取合集视频..."), 4000)
        page.controller.loadSeasonVideos(request, item.isSeries === true, item.id)
    }

    function playSeasonEntries(entries) {
        if (!entries || entries.length === 0) {
            info_bar.showError(qsTr("该合集暂无可播视频"), 3000)
            return
        }
        PlayerController.playRange(entries.map(page.cardForDisplay))
        FluRouter.navigate("/player")
    }

    // ---- 管理弹层 ----
    function openManageDialog() {
        var map = {}
        for (var i = 0; i < ups.length; i++) {
            map[String(ups[i].mid)] = true
        }
        checkedMap = map
        // 弹窗 Loader 每次打开重新创建搜索框，默认空词；此处不能访问其内部 id。
        page.controller.openManage()
        manage_dialog.open()
    }

    function setMemberChecked(mid, name, faceUrl, checked) {
        var map = {}
        for (var key in checkedMap) map[key] = checkedMap[key]
        map[String(mid)] = checked
        checkedMap = map
        page.controller.setMemberChecked(mid, name, faceUrl, checked)
    }

    FluInfoBar {
        id: info_bar

        root: page
    }

    // ---- 冻结标题带(高 56 / 边距 24):大标题 + 三档 chips + 刷新(右缘对齐) ----
    Item {
        id: title_band

        readonly property bool compact: title_text.implicitWidth + tab_row.width + toolbar.width + page.bandRightInset + 32 > width
        height: page.spaceMode ? 0 : (compact ? 96 : 56)
        visible: !page.spaceMode
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        FluText {
            id: title_text

            text: qsTr("特别关注")
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: title_band.compact ? -20 : 0
            }
        }
        Row {
            id: tab_row

            spacing: 6
            anchors {
                left: title_band.compact ? parent.left : title_text.right
                leftMargin: title_band.compact ? 0 : 16
                verticalCenter: parent.verticalCenter
                verticalCenterOffset: title_band.compact ? 22 : 0
            }
            Repeater {
                model: page.tabDefs

                delegate: FluToggleButton {
                    required property var modelData
                    required property int index

                    text: modelData.label
                    checked: page.currentTab === index
                    clickListener: function () {
                        page.selectTab(index)
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
            FluProgressRing {
                id: busy_ring

                indeterminate: true
                strokeWidth: 3
                width: 22
                height: 22
                visible: page.tabBusy || page.controller.seasonVideosBusy
                anchors.verticalCenter: parent.verticalCenter
            }
            FluIconButton {
                id: btn_refresh

                width: 34
                height: 34
                iconSource: FluentIcons.Refresh
                iconSize: 14
                enabled: page.hasUps && !page.tabBusy
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    page.refreshCurrentTab()
                }
            }
        }
    }

    // 小头像的加载/错误态保持同一圆形轮廓，避免通用重试按钮挤出 36/40px 容器。
    Component {
        id: avatar_placeholder
        Image {
            source: "qrc:/images/noface.jpg"
            fillMode: Image.PreserveAspectCrop
            mipmap: true
        }
    }

    // ---- 横向头像条:仅圆形头像(无文字),滚动条不可见;右端固定"管理"入口 ----
    Item {
        id: avatar_band

        height: page.spaceMode ? 0 : 60
        visible: !page.spaceMode
        anchors {
            top: title_band.bottom
            topMargin: page.spaceMode ? 0 : 4
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        Flickable {
            id: avatar_flickable

            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: avatar_row.width
            contentHeight: height
            anchors {
                top: parent.top
                bottom: parent.bottom
                left: parent.left
                right: btn_manage.left
                rightMargin: 8
            }
            Row {
                id: avatar_row

                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: page.ups

                    delegate: Item {
                        id: avatar_item

                        required property var modelData

                        width: 48
                        height: 48
                        Accessible.role: Accessible.Button
                        Accessible.name: String(modelData.name || "")
                        Accessible.onPressAction: page.selectUp(modelData.mid)
                        readonly property bool selected:
                            page.currentMid === avatar_item.modelData.mid
                        // 选中态底色胶囊(互补材质等价实现):与主视图容器卡片底
                        // 视觉连续,自选中头像延伸至主视图的浮层观感
                        Rectangle {
                            anchors.fill: parent
                            radius: 24
                            color: avatar_item.selected
                                       ? Qt.rgba(FluTheme.primaryColor.r,
                                                 FluTheme.primaryColor.g,
                                                 FluTheme.primaryColor.b, 0.30)
                                       : Qt.rgba(0, 0, 0, 0)
                        }
                        FluClip {
                            anchors.centerIn: parent
                            width: 40
                            height: 40
                            radius: [20, 20, 20, 20]
                            FluImage {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                                mipmap: true
                                loadingItem: avatar_placeholder
                                errorItem: avatar_placeholder
                                source: strip_avatar.source.toString() !== ""
                                        ? strip_avatar.source : "qrc:/images/noface.jpg"
                                AvatarSource {
                                    id: strip_avatar
                                    userId: String(avatar_item.modelData.mid)
                                    remoteUrl: Format.ensureDecodableImageUrl(
                                            String(avatar_item.modelData.faceUrl || ""),
                                            AppController.decodableImageFormats)
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.selectUp(avatar_item.modelData.mid)
                            }
                        }
                    }
                }
            }
        }
        FluIconButton {
            id: btn_manage

            width: 40
            height: 40
            radius: 20
            iconSource: FluentIcons.Manage
            iconSize: 15
            enabled: page.upsReady
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            onClicked: {
                page.openManageDialog()
            }
        }
        FluTooltip {
            text: qsTr("管理特别关注")
            visible: btn_manage.hovered
            delay: 300
            x: btn_manage.x + btn_manage.width - implicitWidth
            y: btn_manage.y + btn_manage.height + 6
        }
    }

    // ---- 主视图容器(互补材质突出背景:卡片底浮层,与选中胶囊视觉连续) ----
    FluFrame {
        id: main_container

        radius: 8
        anchors {
            top: avatar_band.bottom
            topMargin: page.spaceMode ? 0 : 8
            left: parent.left
            right: parent.right
            bottom: pagination_host.top
            bottomMargin: 8
            leftMargin: 24
            rightMargin: 24
        }

        // 个人空间的页签位于内容卡片内，保持上方资料区独立。
        Item {
            id: space_tabs
            height: page.spaceMode ? 54 : 0
            visible: page.spaceMode
            anchors { top: parent.top; left: parent.left; right: parent.right }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 6
                Repeater {
                    model: page.tabDefs
                    delegate: FluToggleButton {
                        required property var modelData
                        required property int index
                        text: modelData.label
                        checked: page.currentTab === index
                        clickListener: function () { page.selectTab(index) }
                    }
                }
                Item { Layout.fillWidth: true }
                FluProgressRing {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    strokeWidth: 2
                    visible: page.tabBusy || page.controller.seasonVideosBusy
                }
                FluTextButton {
                    text: qsTr("播放本页")
                    visible: page.currentTab === 0
                    enabled: page.arcFiltered.length > 0 && !page.tabBusy
                    onClicked: page.playSeasonEntries(page.arcFiltered)
                }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16; bottomMargin: 0 }
                height: 1
                color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
            }
        }

        // 宽度同步机制基准见页头 bandRightInset(投稿瀑布流右缘)
        Flickable {
            id: scroll_view

            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: Math.max(content_column.height, 1)
            anchors {
                top: space_tabs.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 16
                rightMargin: 16
                topMargin: 12
                bottomMargin: 12
            }
            // 与其他页同口径:列表滚动容器挂 FluScrollBar
            ScrollBar.vertical: FluScrollBar {}

            Column {
                id: content_column

                width: scroll_view.width
                spacing: 12

                // 分区筛选行(响应 tlist 索引;水平滚动,无滚动条;独立容器)
                RowLayout {
                    id: author_filter_row
                    width: parent.width
                    visible: page.currentTab === 0 || (!page.spaceMode && page.currentUpName !== "")
                    spacing: 10
                    Flickable {
                        visible: page.currentTab === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: partition_row.width
                        contentHeight: height
                        Row {
                            id: partition_row

                            spacing: 6
                            anchors.verticalCenter: parent.verticalCenter
                            FluToggleButton {
                                text: qsTr("全部分区")
                                checked: page.controller.arcTid === 0
                                clickListener: function () {
                                    page.controller.setArcPartition(0)
                                }
                            }
                            Repeater {
                                model: page.controller.arcPartitions

                                delegate: FluToggleButton {
                                    required property var modelData

                                    text: qsTr("%1 (%2)").arg(modelData.name)
                                          .arg(page.formatNumber(modelData.count))
                                    checked: page.controller.arcTid === modelData.tid
                                    clickListener: function () {
                                        page.controller.setArcPartition(modelData.tid)
                                    }
                                }
                            }
                        }
                    }
                    FluComboBox {
                        visible: page.spaceMode && page.currentTab === 0
                        Layout.preferredWidth: 122
                        model: [qsTr("最新发布"), qsTr("最多播放"), qsTr("最多收藏")]
                        currentIndex: page.controller.arcOrder
                        Accessible.name: qsTr("投稿排序")
                        onActivated: page.controller.setArcOrder(currentIndex)
                    }
                    Item {
                        visible: page.currentTab !== 0
                        Layout.fillWidth: true
                    }
                    FluText {
                        id: selected_up_name
                        visible: !page.spaceMode && page.currentUpName !== ""
                        Layout.preferredWidth: Math.min(implicitWidth, 240, parent.width * 0.35)
                        Layout.maximumWidth: Math.min(240, parent.width * 0.35)
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        // 为右侧覆盖式滚动条及其悬停展开状态预留空间。
                        Layout.rightMargin: 24
                        text: page.currentUpName
                        textFormat: Text.PlainText
                        font: FluTextStyle.BodyStrong
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        HoverHandler { id: selected_up_hover }
                        FluTooltip {
                            text: page.currentUpName
                            visible: selected_up_hover.hovered
                            delay: 300
                        }
                    }
                }

                // ============ 投稿档 ============
                Column {
                    visible: page.currentTab === 0
                    width: parent.width
                    spacing: 10

                    // 特别关注保留现有排序按钮，个人空间使用分区旁的下拉框。
                    Row {
                        visible: !page.spaceMode
                        spacing: 6
                        Repeater {
                            model: [
                                { order: 0, label: qsTr("最新发布") },
                                { order: 1, label: qsTr("最多播放") },
                                { order: 2, label: qsTr("最多收藏") }
                            ]

                            delegate: FluToggleButton {
                                required property var modelData

                                text: modelData.label
                                checked: page.controller.arcOrder === modelData.order
                                clickListener: function () {
                                    page.controller.setArcOrder(modelData.order)
                                }
                            }
                        }
                    }
                    // 投稿卡瀑布流(300 定宽列,最短列放置,整体居中)
                    Item {
                        id: arc_masonry

                        width: parent.width
                        height: Math.max(arc_masonry.contentHeight, 1)
                        readonly property int columnWidth: page.spaceMode
                            ? Math.floor((width - columnGap * (columnCount - 1)) / columnCount) : 300
                        readonly property int columnGap: 16
                        readonly property int rowGap: 16
                        readonly property int stride: columnWidth + columnGap
                        readonly property int columnCount:
                            Math.max(1, Math.floor((width + columnGap) / (300 + columnGap)))
                        readonly property int gridWidth:
                            Math.max(0, columnCount * stride - columnGap)
                        property real contentHeight: 0
                        x: Math.max(0, Math.floor((width - gridWidth) / 2))

                        function relayout() {
                            var count = arc_cards.count
                            if (count === 0) {
                                arc_masonry.contentHeight = 0
                                return
                            }
                            var heights = []
                            for (var i = 0; i < count; i++) {
                                var card = arc_cards.itemAt(i)
                                if (!card) continue
                                var col, top
                                if (i < arc_masonry.columnCount) {
                                    col = i
                                    top = 0
                                    heights.push(card.height)
                                } else {
                                    var minHeight = Math.min.apply(null, heights)
                                    col = heights.indexOf(minHeight)
                                    top = minHeight + arc_masonry.rowGap
                                    heights[col] = top + card.height
                                }
                                card.x = col * arc_masonry.stride
                                card.y = top
                            }
                            arc_masonry.contentHeight = Math.max.apply(null, heights)
                        }

                        onColumnCountChanged: relayout()
                        onWidthChanged: relayout()

                        Repeater {
                            id: arc_cards

                            onCountChanged: Qt.callLater(arc_masonry.relayout)

                            model: page.arcFiltered

                            delegate: HistoryCard {
                                id: arc_card

                                cardItem: page.cardForDisplay(modelData)
                                authorNavigationEnabled: page.spaceMode
                                onAuthorClicked: function(author) {
                                    AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                                }
                                width: arc_masonry.columnWidth
                                height: implicitHeight
                                onImplicitHeightChanged:
                                    Qt.callLater(arc_masonry.relayout)
                                Component.onCompleted:
                                    Qt.callLater(arc_masonry.relayout)
                                onCoverClicked: function (sourceItem) {
                                    cover_preview.show(arc_card.baseUrl, sourceItem)
                                }
                            }
                        }
                    }
                    // 刷新失败提示(既有卡片保留,可重试)
                    FluText {
                        visible: !page.controller.arcBusy &&
                                 page.arcError !== "" &&
                                 page.controller.arcItems.length > 0
                        text: qsTr("刷新失败,点击右上角刷新重试")
                        textColor: FluTheme.fontSecondaryColor
                    }
                }

                // ============ 合集档 ============
                Column {
                    visible: page.currentTab === 1
                    width: parent.width
                    spacing: 12

                    // 展开态头部:返回 + 标题 + 角标 + 播放全部
                    Flow {
                        width: parent.width
                        visible: page.expandedSeason !== null
                        spacing: 10
                        FluIconButton {
                            iconSource: FluentIcons.ChevronLeft
                            iconSize: 14
                            width: 34
                            height: 34
                            onClicked: {
                                page.expandedSeason = null
                                page.expandedVideos = []
                                scroll_view.contentY = 0
                            }
                        }
                        FluText {
                            text: page.expandedSeason
                                  ? String(page.expandedSeason.title || "") : ""
                            font: FluTextStyle.BodyStrong
                            width: Math.min(implicitWidth, Math.max(120, parent.width - 54))
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                        FluText {
                            visible: page.expandedVideos.length > 0
                            text: qsTr("共 %1 个视频").arg(
                                      String(page.expandedVideos.length))
                            font: FluTextStyle.Caption
                            textColor: FluTheme.fontSecondaryColor
                        }
                        FluFilledButton {
                            text: qsTr("播放全部")
                            enabled: page.expandedVideos.length > 0 &&
                                     !page.controller.seasonVideosBusy
                            onClicked: {
                                page.playSeasonEntries(page.expandedVideos)
                            }
                        }
                    }
                    // 合集/系列 卡片夹等宽网格(未展开)或视频卡列表(已展开)
                    Item {
                        id: seasons_grid

                        width: parent.width
                        height: Math.max(seasons_grid.contentHeight, 1)
                        // 未展开:卡片夹 170 定宽;展开后:视频卡 300 定宽(HistoryCard 契约)
                        readonly property int cardWidth: page.expandedSeason ? 300 : 170
                        readonly property int columnGap: 16
                        readonly property int stride: cardWidth + columnGap
                        readonly property int columnCount:
                            Math.max(1, Math.floor((width + columnGap) / stride))
                        readonly property int gridWidth:
                            Math.max(0, columnCount * stride - columnGap)
                        property real contentHeight: 0
                        x: Math.max(0, Math.floor((width - gridWidth) / 2))

                        function relayout() {
                            var count = season_cards.count
                            if (count === 0) {
                                seasons_grid.contentHeight = 0
                                return
                            }
                            var y = 0
                            var rowHeight = 0
                            for (var i = 0; i < count; i++) {
                                var card = season_cards.itemAt(i)
                                if (!card) continue
                                var col = i % seasons_grid.columnCount
                                if (col === 0 && i > 0) {
                                    y += rowHeight + seasons_grid.columnGap
                                    rowHeight = 0
                                }
                                card.x = col * seasons_grid.stride
                                card.y = y
                                rowHeight = Math.max(rowHeight, card.height)
                            }
                            seasons_grid.contentHeight = y + rowHeight
                        }

                        onColumnCountChanged: relayout()
                        onStrideChanged: relayout()
                        onWidthChanged: relayout()

                        Repeater {
                            id: season_cards

                            onCountChanged: Qt.callLater(seasons_grid.relayout)

                            model: page.seasonsViewItems

                            delegate: page.expandedSeason
                                          ? season_video_component
                                          : season_folder_component
                        }
                    }
                    FluText {
                        visible: !page.controller.seasonBusy &&
                                 page.seasonError !== "" &&
                                 page.controller.seasonItems.length > 0
                        text: qsTr("刷新失败,点击右上角刷新重试")
                        textColor: FluTheme.fontSecondaryColor
                    }
                }

                // ============ 专栏档 ============
                Column {
                    visible: page.currentTab === 2
                    width: parent.width
                    spacing: 12

                    Item {
                        id: article_grid

                        width: parent.width
                        height: Math.max(article_grid.contentHeight, 1)
                        readonly property int cardWidth: 240
                        readonly property int columnGap: 16
                        readonly property int stride: cardWidth + columnGap
                        readonly property int columnCount:
                            Math.max(1, Math.floor((width + columnGap) / stride))
                        readonly property int gridWidth:
                            Math.max(0, columnCount * stride - columnGap)
                        property real contentHeight: 0
                        x: Math.max(0, Math.floor((width - gridWidth) / 2))

                        function relayout() {
                            var count = article_cards.count
                            if (count === 0) {
                                article_grid.contentHeight = 0
                                return
                            }
                            var y = 0
                            var rowHeight = 0
                            for (var i = 0; i < count; i++) {
                                var card = article_cards.itemAt(i)
                                if (!card) continue
                                var col = i % article_grid.columnCount
                                if (col === 0 && i > 0) {
                                    y += rowHeight + article_grid.columnGap
                                    rowHeight = 0
                                }
                                card.x = col * article_grid.stride
                                card.y = y
                                rowHeight = Math.max(rowHeight, card.height)
                            }
                            article_grid.contentHeight = y + rowHeight
                        }

                        onColumnCountChanged: relayout()
                        onWidthChanged: relayout()

                        Repeater {
                            id: article_cards

                            onCountChanged: Qt.callLater(article_grid.relayout)

                            model: page.articleFiltered

                            delegate: Item {
                                id: article_card

                                required property var modelData

                                width: article_grid.cardWidth
                                height: info_column.height + 12
                                readonly property string coverUrl:
                                    String(article_card.modelData.coverUrl || "")
                                readonly property string baseUrl:
                                    Format.ensureDecodableImageUrl(
                                            Format.stripImageTranscode(coverUrl),
                                            AppController.decodableImageFormats)

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    color: article_mouse.containsMouse
                                               ? FluTheme.itemHoverColor
                                               : Qt.rgba(0, 0, 0, 0)
                                    border.color: article_mouse.containsMouse
                                                      ? FluTheme.primaryColor
                                                      : Qt.rgba(0, 0, 0, 0)
                                    border.width: 1
                                }
                                MouseArea {
                                    id: article_mouse

                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // 点击卡片:浏览器打开该文集页(规约)
                                        var link = String(
                                                    article_card.modelData.linkUrl || "")
                                        if (link !== "") Qt.openUrlExternally(link)
                                    }
                                }
                                Column {
                                    id: info_column

                                    anchors {
                                        top: parent.top
                                        left: parent.left
                                        right: parent.right
                                        topMargin: 6
                                    }
                                    Item {
                                        id: cover_area

                                        width: parent.width
                                        height: Math.round(width * 9 / 16)
                                        FluImage {
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            source: Format.cardCoverThumbnailUrl(article_card.coverUrl)
                                        }
                                    }
                                    Column {
                                        width: parent.width
                                        topPadding: 8
                                        spacing: 4
                                        FluText {
                                            width: parent.width
                                            text: String(
                                                    article_card.modelData.title || "")
                                            font.pixelSize: 13
                                            font.bold: true
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }
                                        FluText {
                                            width: parent.width
                                            text: qsTr("%1 篇文章 · %2 阅读 · %3 字")
                                                  .arg(page.formatNumber(
                                                           article_card.modelData.articlesCount))
                                                  .arg(page.formatNumber(
                                                           article_card.modelData.read))
                                                  .arg(page.formatNumber(
                                                           article_card.modelData.words))
                                            font: FluTextStyle.Caption
                                            textColor: FluTheme.fontSecondaryColor
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                    FluText {
                        visible: !page.controller.articleBusy &&
                                 page.articleError !== "" &&
                                 page.controller.articleItems.length > 0
                        text: qsTr("刷新失败,点击右上角刷新重试")
                        textColor: FluTheme.fontSecondaryColor
                    }
                }
            }

            // 当前档"已载条目数"(搜索未匹配提示口径:含当前页条数)
            readonly property int loadedCount: {
                if (page.currentTab === 0) return page.controller.arcItems.length
                if (page.currentTab === 1) {
                    return page.expandedSeason ? page.expandedVideos.length
                                               : page.controller.seasonItems.length
                }
                return page.articleSlice.length
            }
        }

        // 空态定位到可视区域,避免以 Flickable 的空 contentItem 高度居中而越过上边缘。
        // 投稿筛选/排序与展开合集标题继续占据各自区域,空态只使用下方剩余空间。
        Item {
            anchors.fill: scroll_view
            anchors.topMargin: Math.min(Math.max(0, scroll_view.height),
                                        (author_filter_row.visible ? author_filter_row.height + content_column.spacing : 0)
                                        + (page.currentTab === 0 ? arc_masonry.y :
                                           (page.currentTab === 1 ? seasons_grid.y : 0)))
            clip: true

            // ---- 空态:装载中 / 失败(可重试) / 搜索未匹配 / 无数据 ----
            FluText {
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: {
                    if (!page.upsReady || !page.hasUps) return false
                    if (page.currentTab === 0) return page.arcFiltered.length === 0
                    if (page.currentTab === 1) return page.seasonsViewItems.length === 0
                    return page.articleFiltered.length === 0
                }
                text: {
                    if (page.currentTab === 0) {
                        if (page.controller.arcBusy) return qsTr("正在加载投稿...")
                        if (page.arcError !== "") {
                            return page.controller.unauthorized
                                        ? qsTr("登录失效,点击右上角刷新重试")
                                        : qsTr("加载失败,点击右上角刷新重试")
                        }
                        if (page.searching) {
                            return qsTr("未找到匹配(已载 %1 条)")
                                    .arg(String(scroll_view.loadedCount))
                        }
                        return qsTr("该 UP 暂无投稿")
                    }
                    if (page.currentTab === 1) {
                        if (page.controller.seasonBusy) return qsTr("正在加载合集...")
                        if (page.seasonError !== "") {
                            return page.controller.unauthorized
                                        ? qsTr("登录失效,点击右上角刷新重试")
                                        : qsTr("加载失败,点击右上角刷新重试")
                        }
                        if (page.searching) {
                            return qsTr("未找到匹配(已载 %1 条)")
                                    .arg(String(scroll_view.loadedCount))
                        }
                        if (page.expandedSeason !== null && page.expandedVideos.length === 0) {
                            return page.controller.seasonVideosBusy
                                        ? qsTr("正在拉取合集视频...")
                                        : qsTr("该合集暂无视频")
                        }
                        return qsTr("该 UP 暂无合集与系列")
                    }
                    if (page.controller.articleBusy) return qsTr("正在加载专栏文集...")
                    if (page.articleError !== "") {
                        return page.controller.unauthorized
                                    ? qsTr("登录失效,点击右上角刷新重试")
                                    : qsTr("加载失败,点击右上角刷新重试")
                    }
                    if (page.searching) {
                        return qsTr("未找到匹配(已载 %1 条)")
                                .arg(String(scroll_view.loadedCount))
                    }
                    return qsTr("该 UP 暂无专栏文集")
                }
                textColor: FluTheme.fontSecondaryColor
            }
        }

        // UP 列表空态/装载中引导(骨架可交互,管理按钮不阻塞)
        FluText {
            anchors.centerIn: parent
            visible: !page.spaceMode && page.upsReady && !page.hasUps
            text: qsTr("暂无特别关注,点击头像条右端「管理」添加")
            textColor: FluTheme.fontSecondaryColor
        }
        FluText {
            anchors.centerIn: parent
            visible: !page.spaceMode && !page.upsReady
            text: qsTr("正在装载特别关注列表...")
            textColor: FluTheme.fontSecondaryColor
        }
    }

    // ---- 分页栏:首页/上一页/数字/下一页/末页 + 口径文案(按档绑定;展开态隐藏) ----
    Column {
        id: pagination_host

        width: parent.width - 48
        spacing: 4
        height: visible ? implicitHeight : 0
        visible: !(page.currentTab === 1 && page.expandedSeason !== null)
        enabled: !page.tabBusy
        opacity: enabled ? 1 : 0.5
        anchors {
            bottom: parent.bottom
            bottomMargin: 12
            horizontalCenter: parent.horizontalCenter
        }
        FluText {
            function indicator() {
                var n = page.pageInfoTotal
                if (n <= 0) return qsTr("暂无记录")
                var totalPages = Math.max(1, Math.ceil(n / page.pageSize))
                var current = Math.max(1, page.pageInfoPage)
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

            itemCount: page.pageInfoTotal
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
                if (page.currentTab === 0) {
                    page.controller.loadArcPage(requestedPage)
                } else if (page.currentTab === 1) {
                    page.controller.loadSeasonsPage(requestedPage)
                } else {
                    page.controller.setArticlePage(requestedPage)  // 本地切片
                }
            }
        }
    }

    // ---- 回到顶部 FAB ----
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

    // 封面预览层:全页覆盖(投稿卡与合集视频卡共用)
    CoverPreviewOverlay {
        id: cover_preview

        anchors.fill: parent
        z: 900
    }

    // ---- 合集卡片夹 delegate(三张部分重叠卡片 + 中央播放按钮 + 标题/角标) ----
    Component {
        id: season_folder_component

        Item {
            id: folder_root

            required property var modelData

            width: seasons_grid.cardWidth
            height: stack_area.height + info_column.height + 12
            readonly property string coverUrl:
                String(folder_root.modelData.coverUrl || "")
            readonly property string baseUrl:
                Format.ensureDecodableImageUrl(
                        Format.stripImageTranscode(coverUrl),
                        AppController.decodableImageFormats)
            readonly property string kindLabel:
                folder_root.modelData.isSeries === true ? qsTr("系列") : qsTr("合集")
            readonly property string totalCount:
                page.formatNumber(folder_root.modelData.total)

            MouseArea {
                id: folder_mouse

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.openSeason(folder_root.modelData)
            }

            // 三张部分重叠卡片:首张 = 合集封面;后两张 = 半透明灰色带描边卡
            Item {
                id: stack_area

                width: parent.width
                height: cover_card.height + 16
                Rectangle {
                    x: 16
                    y: 16
                    width: cover_card.width
                    height: cover_card.height
                    radius: 8
                    color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.06)
                                         : Qt.rgba(0, 0, 0, 0.06)
                    border.color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.25)
                                                : Qt.rgba(0, 0, 0, 0.18)
                    border.width: 1
                }
                Rectangle {
                    x: 8
                    y: 8
                    width: cover_card.width
                    height: cover_card.height
                    radius: 8
                    color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.10)
                                         : Qt.rgba(0, 0, 0, 0.08)
                    border.color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.30)
                                                : Qt.rgba(0, 0, 0, 0.22)
                    border.width: 1
                }
                Rectangle {
                    id: cover_card

                    x: 0
                    y: 0
                    width: parent.width - 16
                    height: Math.round(width * 9 / 16)
                    radius: 8
                    color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.05)
                                         : Qt.rgba(0, 0, 0, 0.04)
                    border.color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.15)
                                                : Qt.rgba(0, 0, 0, 0.10)
                    border.width: 1
                    clip: true
                    FluImage {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        source: Format.cardCoverThumbnailUrl(folder_root.coverUrl)
                    }
                    // 中央播放按钮:仅触发播放序列,事件被本层消费,
                    // 不得冒泡至卡片跳转处理(互斥契约)
                    FluIconButton {
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        radius: 18
                        iconSource: FluentIcons.Play
                        iconSize: 14
                        onClicked: page.playSeasonAll(folder_root.modelData)
                    }
                }
                HoverHandler {
                    id: folder_hover
                }
                FluTooltip {
                    text: String(folder_root.modelData.title || "")
                    visible: folder_hover.hovered
                    delay: 300
                }
            }
            Column {
                id: info_column

                anchors {
                    top: stack_area.bottom
                    left: parent.left
                    right: parent.right
                    topMargin: 8
                }
                spacing: 4
                FluText {
                    width: parent.width
                    text: String(folder_root.modelData.title || "")
                    font.pixelSize: 13
                    font.bold: true
                    wrapMode: Text.NoWrap
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                Row {
                    spacing: 6
                    Rectangle {
                        radius: 3
                        color: FluTheme.itemHoverColor
                        width: kind_text.implicitWidth + 12
                        height: kind_text.implicitHeight + 2
                        anchors.verticalCenter: parent.verticalCenter
                        FluText {
                            id: kind_text

                            anchors.centerIn: parent
                            text: folder_root.kindLabel
                            font: FluTextStyle.Caption
                            opacity: 0.9
                        }
                    }
                    FluText {
                        text: qsTr("%1 个视频").arg(folder_root.totalCount)
                        font: FluTextStyle.Caption
                        textColor: FluTheme.fontSecondaryColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    // ---- 展开合集内的视频卡 delegate(HistoryCard 整链:预览/主体起播/右键打开链接) ----
    Component {
        id: season_video_component

        HistoryCard {
            id: video_card

            cardItem: page.cardForDisplay(modelData)
            authorNavigationEnabled: page.spaceMode
                                onAuthorClicked: function(author) {
                                    AppController.openUserSpace(author.mid, author.name, author.faceUrl)
                                }
            width: seasons_grid.cardWidth
            height: implicitHeight
            onImplicitHeightChanged: Qt.callLater(seasons_grid.relayout)
            Component.onCompleted: Qt.callLater(seasons_grid.relayout)
            onCoverClicked: function (sourceItem) {
                cover_preview.show(video_card.baseUrl, sourceItem)
            }
        }
    }

    // ---- 管理弹层:分页浏览 + 服务端搜索(400ms 防抖)+ 勾选池 + 合并保存 ----
    FluContentDialog {
        id: manage_dialog

        property bool searchMode: page.controller.manageSearchMode

        title: qsTr("管理特别关注")
        implicitWidth: 520
        buttonFlags: FluContentDialogType.NegativeButton |
                     FluContentDialogType.PositiveButton
        positiveText: qsTr("保存")
        negativeText: qsTr("取消")
        onPositiveClicked: {
            page.controller.saveManage()
        }
        contentDelegate: Component {
            Item {
                implicitHeight: 440

                Column {
                    anchors.fill: parent
                    spacing: 10

                    FluTextBox {
                        id: manage_search

                        width: parent.width
                        placeholderText: qsTr("搜索 UP 主昵称(服务端)")
                        onTextChanged: {
                            if (manage_dialog.visible) search_debounce.restart()
                        }
                    }
                    Timer {
                        id: search_debounce

                        interval: 400  // 短防抖:停顿到期才发起服务端搜索
                        onTriggered: {
                            page.controller.manageSearch(manage_search.text)
                        }
                    }
                    // 成员列表:浏览模式 = 已装载分页累积;搜索模式 = 服务端结果
                    ListView {
                        id: member_list

                        width: parent.width
                        height: parent.height - manage_search.height - load_more_row.height - 32
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: page.controller.manageSearchMode
                                   ? page.controller.manageSearchResults
                                   : page.controller.manageMembers
                        delegate: Item {
                            id: member_item

                            required property var modelData

                            width: member_list.width
                            height: 52
                            readonly property bool checked:
                                page.checkedMap[String(member_item.modelData.mid)] === true
                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                color: member_mouse.containsMouse
                                           ? FluTheme.itemHoverColor
                                           : Qt.rgba(0, 0, 0, 0)
                            }
                            // 行点击切换勾选;声明于内容行之前,复选框自身点击不被吞
                            MouseArea {
                                id: member_mouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    page.setMemberChecked(
                                                member_item.modelData.mid,
                                                String(member_item.modelData.name || ""),
                                                String(member_item.modelData.faceUrl || ""),
                                                !member_item.checked)
                                }
                            }
                            Row {
                                spacing: 10
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 4
                                    rightMargin: 4
                                }
                                FluClip {
                                    width: 36
                                    height: 36
                                    radius: [18, 18, 18, 18]
                                    anchors.verticalCenter: parent.verticalCenter
                                    FluImage {
                                        anchors.fill: parent
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        smooth: true
                                        mipmap: true
                                        loadingItem: avatar_placeholder
                                        errorItem: avatar_placeholder
                                        source: member_avatar.source.toString() !== ""
                                                ? member_avatar.source : "qrc:/images/noface.jpg"
                                        AvatarSource {
                                            id: member_avatar
                                            userId: String(member_item.modelData.mid)
                                            remoteUrl: Format.ensureDecodableImageUrl(
                                                    String(member_item.modelData.faceUrl || ""),
                                                    AppController.decodableImageFormats)
                                        }
                                    }
                                }
                                Column {
                                    width: parent.width - 36 - check_box.width - 44
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    FluText {
                                        width: parent.width
                                        text: String(member_item.modelData.name || "")
                                        font.pixelSize: 13
                                        font.bold: true
                                        wrapMode: Text.NoWrap
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                    FluText {
                                        width: parent.width
                                        visible: String(member_item.modelData.sign || "") !== ""
                                        text: String(member_item.modelData.sign || "")
                                        font: FluTextStyle.Caption
                                        textColor: FluTheme.fontSecondaryColor
                                        wrapMode: Text.NoWrap
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                }
                                FluCheckBox {
                                    id: check_box

                                    checked: member_item.checked
                                    anchors.verticalCenter: parent.verticalCenter
                                    clickListener: function () {
                                        page.setMemberChecked(
                                                    member_item.modelData.mid,
                                                    String(member_item.modelData.name || ""),
                                                    String(member_item.modelData.faceUrl || ""),
                                                    !member_item.checked)
                                    }
                                }
                            }
                        }
                        ScrollBar.vertical: FluScrollBar {}
                    }
                    Row {
                        id: load_more_row

                        width: parent.width
                        spacing: 10
                        FluText {
                            readonly property string countText: {
                                if (page.controller.manageSearchMode) {
                                    return qsTr("匹配 %1 个 · 已载 %2")
                                            .arg(String(page.controller.manageSearchTotal))
                                            .arg(String(page.controller.manageSearchResults.length))
                                }
                                return qsTr("已载 %1 / 共 %2")
                                        .arg(String(page.controller.manageMembers.length))
                                        .arg(String(page.controller.manageBrowseTotal))
                            }
                            text: page.controller.manageError !== ""
                                      ? qsTr("加载失败:%1").arg(
                                            page.controller.manageError)
                                      : countText
                            textColor: page.controller.manageError !== ""
                                           ? FluTheme.fontPrimaryColor
                                           : FluTheme.fontSecondaryColor
                            wrapMode: Text.WrapAnywhere
                            width: Math.max(0, parent.width - load_more_btn.width - (manage_busy_ring.visible ? manage_busy_ring.width + parent.spacing : 0) - parent.spacing)
                            anchors.verticalCenter: parent.verticalCenter
                            maximumLineCount: 2
                            font.pixelSize: 12
                        }
                        FluProgressRing {
                            id: manage_busy_ring
                            indeterminate: true
                            strokeWidth: 3
                            width: 20
                            height: 20
                            visible: page.controller.manageBusy
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        FluButton {
                            id: load_more_btn

                            text: qsTr("加载更多")
                            enabled: !page.controller.manageBusy &&
                                     (!page.controller.manageSearchMode ||
                                      page.controller.manageSearchPage < 5)
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                // 失败后重试同一动作(加载更多兼做重试)
                                page.controller.manageLoadMore()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Controller 回投接线 ----
    Connections {
        target: page.controller

        function onCurrentMidChanged() {
            if (page.spaceMode) {
                page.searchQuery = ""
                page.currentTab = 0
            }
            page.arcError = ""
            page.seasonError = ""
            page.articleError = ""
            page.expandedSeason = null
            page.expandedVideos = []
            page.pendingSeasonRequest = 0
            page.pendingSeasonAction = 0
            scroll_view.contentY = 0
            page.syncPaginationBar()
            page.controller.ensureCurrentTabLoaded(page.currentTab)
        }
        function onArcItemsChanged() {
            page.arcError = ""
            page.syncPaginationBar()
        }
        function onSeasonItemsChanged() {
            page.seasonError = ""
            page.syncPaginationBar()
        }
        function onArticleInfoChanged() {
            page.articleError = ""
            page.syncPaginationBar()
        }
        function onLoadFailed(message) {
            // 失败仅来自当前档的用户动作(刷新/翻页/切档首拉)
            if (page.currentTab === 0) page.arcError = message
            else if (page.currentTab === 1) page.seasonError = message
            else page.articleError = message
            info_bar.showError(message, 3000)
        }
        function onSeasonVideosReady(requestId, entries) {
            if (requestId !== page.pendingSeasonRequest) return
            page.pendingSeasonRequest = 0
            if (page.pendingSeasonAction === 1) {
                // 播放全部:批量入列(去重保位)→ 自首集起播;窗口复用
                page.playSeasonEntries(entries)
            } else {
                page.expandedVideos = entries
                if (entries.length === 0) {
                    info_bar.showError(qsTr("该合集暂无视频"), 3000)
                }
            }
        }
        function onSeasonVideosFailed(message) {
            page.pendingSeasonRequest = 0
            info_bar.showError(message, 3000)
        }
        function onManageSaved() {
            info_bar.showSuccess(qsTr("已保存特别关注列表"), 3000)
        }
        function onSeedFailed(message) {
            // 种子导入失败:本地化提示;骨架仍可交互,空态引导指向管理入口
            info_bar.showError(qsTr("特别关注分组导入失败,可通过「管理」手动添加"),
                               4000)
        }
    }

    Component.onCompleted: {
        // 首次进入装载本地快照(文件不存在时一次性种子导入);切页返回零开销
        if (!page.spaceMode) page.controller.ensureReady()
        page.controller.ensureCurrentTabLoaded(page.currentTab)
        page.syncPaginationBar()
    }
}
