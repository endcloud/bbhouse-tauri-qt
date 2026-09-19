import QtQuick
import QtQuick.Layouts
import FluentUI
// 自导入:pages/ 子目录类型(PlaceholderPage 等)与 MainWindow 不同目录,
// 同模块内也需显式 import 才能按名引用
import bbhouse

// 主窗口导航外壳(app-navigation-shell):FluWindow 自定义标题栏(48px)+
// FluNavigationView 左窄轨(默认折叠)+ 标题栏居中搜索框(仅卡片页)+
// 页面路由(Loader 缓存宿主,等价 NavigationCache:首访创建、切走保留、重选不重建)。
FluWindow {
    id: window

    width: 1200
    height: 720
    minimumWidth: 880
    minimumHeight: 560
    launchMode: FluWindowType.SingleTask
    title: qsTr("B站历史记录")

    // 当前页 key:页面宿主切换 + 搜索框可见性条件
    property string currentPage: "dynamics"
    readonly property var pageKeys: ["dynamics", "special", "watchlater", "bangumi", "live", "online", "local", "settings", "popular", "downloads", "about"]
    // 标题栏搜索框只在卡片页呈现(动态/特别关注/稍后再看/番剧/在线历史/本地历史;
    // 特别关注三档均支持标题/UP 名称的当前页投影,special-follow-ui 搜索契约)
    readonly property var cardPages: ["dynamics", "special", "watchlater", "bangumi", "live", "online", "local", "popular", "downloads"]
    property bool spaceVisible: false
    property bool spaceVisited: false
    readonly property bool searchVisible: spaceVisible || cardPages.indexOf(currentPage) !== -1
    readonly property var activeSearchPage: {
        if (spaceVisible) return space_loader.item
        if (cardPages.indexOf(currentPage) === -1) return null
        var loader = page_host.children[pageKeys.indexOf(currentPage)]
        return loader ? loader.item : null
    }

    function finishSearch() {
        var editor = useSystemAppBar ? search_box_mac : search_box
        editor.submit()
        editor.focus = false
    }

    // 页面缓存:首访才创建,此后常驻(本地历史页码/同步状态跨页保持)
    property var visitedPages: []

    // 窗格折叠仅由汉堡按钮触发(窗口尺寸变化不改变折叠态,规约口径);
    // Compact=仅图标窄轨(50px),Open=图标+文字展开态(FluNavigationViewType)
    property bool navExpanded: false

    function visitPage(key) {
        if (pageKeys.indexOf(key) === -1) return
        // 先提交旧页再切换绑定，避免失焦信号晚到时把旧文字写进新页。
        finishSearch()
        if (visitedPages.indexOf(key) === -1) {
            visitedPages = visitedPages.concat(key)
        }
        spaceVisible = false
        currentPage = key
    }

    function openUserSpace(mid, name, faceUrl) {
        if (Number(mid) <= 0) return
        finishSearch()
        // Same author within their space is already the current destination.
        if (spaceVisible && UserSpaceController.profileMid === String(mid)) return
        var changed = UserSpaceController.profileMid !== String(mid)
        UserSpaceController.openSpace(mid, name, faceUrl)
        spaceVisited = true
        if (changed && space_loader.item) space_loader.item.resetView()
        spaceVisible = true
    }

    function returnFromSpace() {
        if (!spaceVisible) return
        finishSearch()
        spaceVisible = false
    }

    Connections {
        target: AppController
        function onUserSpaceRequested(mid, name, faceUrl) {
            window.openUserSpace(mid, name, faceUrl)
        }
    }
    Connections {
        target: LoginController
        function onAuthenticated() {
            // A re-login must retry a feed that previously failed authorization.
            var feedAlreadyVisited = window.visitedPages.indexOf("dynamics") !== -1
            window.visitPage("dynamics")
            nav_view.setCurrentIndex(0)
            if (feedAlreadyVisited) DynamicsController.refresh()
        }
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: window.spaceVisible
        onActivated: window.returnFromSpace()
    }

    Component.onCompleted: {
        // 主题即时生效:FluThemeType.DarkMode = System 0 / Light 1 / Dark 2(Def.h)
        FluTheme.darkMode = Qt.binding(function () {
            if (AppController.theme === "dark") return 2
            if (AppController.theme === "light") return 1
            return 0
        })
        // 稍后登录进入本地媒体库，不创建默认动态页或发出账号请求。
        nav_view.setCurrentIndex(LoginController.needsLogin ? 9 : 0)
        smokeNavigate()
    }

    appBar: FluAppBar {
        id: app_bar

        height: 48
        // 规约:应用标题与图标隐藏,避免与搜索框在窗格展开时重叠
        titleVisible: false
        icon: ""
        showDark: false
        showStayTop: false

        // 窗格折叠开关位于标题栏最左侧(FluFrameless 无双击最大化逻辑,
        // setHitTestVisible 后按钮可点、其余区域保持拖拽)
        FluIconButton {
            id: btn_nav_toggle

            width: 38
            height: 38
            anchors {
                left: parent.left
                leftMargin: 5
                verticalCenter: parent.verticalCenter
            }
            iconSource: FluentIcons.GlobalNavButton
            iconSize: 15
            onClicked: {
                window.navExpanded = !window.navExpanded
            }
        }

        // 标题栏居中搜索框:仅卡片页显示，状态归当前缓存页面所有。
        PageSearchBox {
            id: search_box

            width: 320
            height: 30
            anchors.centerIn: parent
            visible: window.searchVisible
            placeholderText: qsTr("搜索标题或 UP 主")
            searchPage: window.activeSearchPage
        }

        Component.onCompleted: {
            if (window.useSystemAppBar) return  // 系统标题栏:无 frameless 命中托管
            window.setHitTestVisible(btn_nav_toggle)
            window.setHitTestVisible(search_box)
        }
    }

    // macOS 系统标题栏下,汉堡开关与标题栏搜索框下沉到内容区顶部工具行
    // (Windows 形态仍由自绘 appBar 承载,该行高度为 0)
    Item {
        id: mac_toolbar

        width: parent.width
        height: window.useSystemAppBar ? 44 : 0
        visible: window.useSystemAppBar

        FluIconButton {
            id: btn_nav_toggle_mac

            width: 38
            height: 38
            iconSource: FluentIcons.GlobalNavButton
            iconSize: 15
            anchors {
                left: parent.left
                leftMargin: 5
                verticalCenter: parent.verticalCenter
            }
            onClicked: {
                window.navExpanded = !window.navExpanded
            }
        }
        PageSearchBox {
            id: search_box_mac

            width: 320
            height: 30
            anchors.centerIn: parent
            visible: window.searchVisible
            placeholderText: qsTr("搜索标题或 UP 主")
            searchPage: window.activeSearchPage
        }
    }

    // MouseArea/TabFocus 按钮不会自动移走 TextField 焦点。被动观察按下，
    // 不抢占按钮、卡片或 Flickable 的事件，框外点击也能提交搜索。
    TapHandler {
        parent: window.contentItem
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        gesturePolicy: TapHandler.DragThreshold
        onPressedChanged: {
            if (!pressed) return
            var editor = window.useSystemAppBar ? search_box_mac : search_box
            if (!editor.activeFocus) return
            var local = editor.mapFromItem(parent, point.position.x, point.position.y)
            if (!editor.contains(local)) window.finishSearch()
        }
    }

    FluNavigationView {
        id: nav_view

        width: parent.width
        y: mac_toolbar.height
        height: parent.height - mac_toolbar.height
        // 内置导航条隐藏:折叠开关由标题栏最左侧汉堡按钮承担(app-navigation-shell 规约)
        hideNavAppBar: true
        displayMode: window.navExpanded ? FluNavigationViewType.Open : FluNavigationViewType.Compact

        items: FluObject {
            FluPaneItem {
                title: qsTr("动态")
                icon: FluentIcons.ReadingList  // glyph E7BC
                onTap: {
                    window.visitPage("dynamics")
                }
            }
            FluPaneItem {
                title: qsTr("流行")
                icon: FluentIcons.AreaChart
                onTap: window.visitPage("popular")
            }
            FluPaneItem {
                title: qsTr("番剧")
                icon: FluentIcons.Movies  // glyph E8B2
                onTap: {
                    window.visitPage("bangumi")
                }
            }
            FluPaneItem {
                title: qsTr("直播")
                icon: FluentIcons.Webcam
                onTap: window.visitPage("live")
            }
            FluPaneItem {
                title: qsTr("特别关注")
                icon: FluentIcons.Pin  // glyph E718
                onTap: {
                    window.visitPage("special")
                }
            }
            FluPaneItemSeparator {}
            FluPaneItem {
                title: qsTr("稍后再看")
                icon: FluentIcons.Recent  // glyph E823
                onTap: {
                    window.visitPage("watchlater")
                }
            }
            FluPaneItem {
                title: qsTr("在线历史")
                icon: FluentIcons.History  // glyph E81C
                onTap: {
                    window.visitPage("online")
                }
            }
            FluPaneItem {
                title: qsTr("本地历史")
                icon: FluentIcons.PC1  // glyph E977
                onTap: {
                    window.visitPage("local")
                }
            }
            FluPaneItem {
                title: qsTr("下载管理")
                icon: FluentIcons.Download
                onTap: window.visitPage("downloads")
            }
        }

        footerItems: FluObject {
            FluPaneItem {
                title: qsTr("设置")
                icon: FluentIcons.Settings  // glyph E713
                onTap: {
                    window.visitPage("settings")
                }
            }
            FluPaneItem {
                title: qsTr("关于")
                icon: FluentIcons.Info
                onTap: window.visitPage("about")
            }
        }
    }

    // 页面宿主:左缘跟随窗格宽度(折叠窄轨 50 / 展开态 cellWidth 300)
    StackLayout {
        id: page_host

        anchors {
            top: mac_toolbar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            leftMargin: window.navExpanded ? nav_view.cellWidth : nav_view.navCompactWidth
        }
        visible: !window.spaceVisible
        currentIndex: window.pageKeys.indexOf(window.currentPage)

        Loader {
            active: window.visitedPages.indexOf("dynamics") !== -1
            sourceComponent: DynamicsPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("special") !== -1
            sourceComponent: SpecialFollowPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("watchlater") !== -1
            sourceComponent: WatchlaterPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("bangumi") !== -1
            sourceComponent: BangumiPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("live") !== -1
            sourceComponent: LivePage {}
        }
        Loader {
            active: window.visitedPages.indexOf("online") !== -1
            sourceComponent: OnlineHistoryPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("local") !== -1
            sourceComponent: LocalHistoryPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("settings") !== -1
            sourceComponent: SettingsPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("popular") !== -1
            sourceComponent: PopularPage {}
        }
        Loader {
            active: window.visitedPages.indexOf("downloads") !== -1
            sourceComponent: DownloadsPage {
                Component.onCompleted: if (LoginController.needsLogin) showingLibrary = true
            }
        }
        Loader {
            active: window.visitedPages.indexOf("about") !== -1
            sourceComponent: AboutPage {}
        }
    }

    DownloadDialog { id: download_dialog }
    Connections {
        target: DownloadController
        function onDownloadRequested(entry) { download_dialog.showFor(entry) }
        function onErrorChanged() {
            if (DownloadController.error !== "") window.showError(DownloadController.error, 6000)
        }
    }

    Loader {
        id: space_loader
        anchors.fill: page_host
        active: window.spaceVisited
        visible: window.spaceVisible
        sourceComponent: UserSpacePage {
            onBackRequested: window.returnFromSpace()
        }
    }

    // 冒烟导航:BBHOUSE_SMOKE_NAV=qml 文件名列表(逗号分隔),逐页导航;
    // 走与真实点击一致的 visitPage 路由,页面装载错误由 stderr 捕获
    function smokeNavigate() {
        var nav = (typeof bbhouseSmokeNav !== "undefined" && bbhouseSmokeNav) ? String(bbhouseSmokeNav) : ""
        if (nav === "") return
        var targets = {
            "PlaceholderPage.qml": ["special"],
            "SpecialFollowPage.qml": ["special"],
            "DynamicsPage.qml": ["dynamics"],
            "LocalHistoryPage.qml": ["local"],
            "OnlineHistoryPage.qml": ["online"],
            "WatchlaterPage.qml": ["watchlater"],
            "BangumiPage.qml": ["bangumi"],
            "LivePage.qml": ["live"],
            "PopularPage.qml": ["popular"],
            "SettingsPage.qml": ["settings"],
            "DownloadsPage.qml": ["downloads"],
            "AboutPage.qml": ["about"]
        }
        var names = nav.split(",")
        for (var i = 0; i < names.length; i++) {
            if (names[i].trim() === "UserSpacePage.qml") {
                // No account/network fixture is needed to validate page construction.
                spaceVisited = true
                spaceVisible = true
                continue
            }
            var keys = targets[names[i].trim()]
            if (!keys) continue
            for (var j = 0; j < keys.length; j++) {
                visitPage(keys[j])
            }
        }
    }
}
