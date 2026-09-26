import QtQuick
import FluentUI
import "../js/Format.js" as Format

// 历史卡片(history-browser-ui 卡片信息元素契约):
// 封面(16:9)+ 时长角标(右下)+ 观看进度条(底部)+ 标题(最多两行)+
// 副标题(≠主标题才显)+ UP 主 + 业务类型标签 + 最近观看时间 + 进度文本。
// 缩略图统一使用 CDN 400×225 WebP；失败显示重试，不自动请求大尺寸原图。
// 交互:点击视频主体 → 在播放窗口起播;非视频主体 → 浏览器打开 linkUrl;
// 点击封面 → 仅发 coverClicked(由宿主页打开封面预览,与起播/跳转互斥);
// 右键 FluMenu 保留打开链接。
// 宽度由瀑布流列设定(默认 300,定宽列契约)。
Item {
    id: card_root

    property var cardItem: ({})
    property bool authorNavigationEnabled: true
    // 发现页没有观看记录；统计单独呈现，不能借用 viewCount 的观看次数角标。
    property bool showRecordedBadge: false
    property bool showHistoryTime: true
    property bool showAuthor: true
    property string statisticsText: ""
    property bool seasonPlaybackEnabled: false
    property bool playbackOverride: false
    property bool playbackEnabled: true
    property bool downloadEnabled: true
    property bool contextMenuOverride: false
    signal playbackRequested(var item)
    signal coverClicked(var sourceItem)
    signal authorClicked(var author)
    signal seasonPlayRequested(var item)
    signal contextMenuRequested(var sourceItem)

    width: 300
    implicitHeight: Math.ceil(cover_area.height + text_block.height)

    // ---- 字段快捷口径(原型 Models.cs) ----
    readonly property string titleText: String(cardItem.title || "")
    readonly property string subtitleText: String(cardItem.subtitle || "")
    // 副标题仅在与主标题不同时展示(单 P 视频不重复渲染)
    readonly property string displaySubtitle:
        subtitleText.trim() !== "" && subtitleText.trim() !== titleText.trim()
            ? subtitleText : ""
    readonly property string coverUrl: String(cardItem.coverUrl || "")
    readonly property string linkUrl: String(cardItem.linkUrl || "")
    readonly property string business: String(cardItem.business || "")
    readonly property string badgeText: String(cardItem.badge || "")
    readonly property string authorLine: {
        var name = String(cardItem.authorName || "").trim()
        return name === "" ? qsTr("未知作者") : name
    }
    // mid 使用十进制字符串跨越 QML 边界，避免 32 位截断或大整数舍入。
    readonly property string authorMid: String(cardItem.authorMid || "")
    readonly property bool authorNavigable: authorNavigationEnabled && videoEntry &&
                                            /^[0-9]+$/.test(authorMid) && /[1-9]/.test(authorMid)
    readonly property var viewAtValue: cardItem.viewAt ? cardItem.viewAt : 0
    readonly property int progressValue: cardItem.progress ? cardItem.progress : 0
    readonly property int durationValue: cardItem.duration ? cardItem.duration : 0
    readonly property int viewCountValue: cardItem.viewCount ? cardItem.viewCount : 0
    readonly property double oidValue: cardItem.oid ? cardItem.oid : 0
    readonly property var viewRecords: cardItem.viewRecords || []
    // 稍后再看扩展键(WatchlaterController 注入;历史条目无此键,语义不变):
    // epId/cid 成对 → PGC 条目免 season 换算直起播(PlayerController 的 epId+cid
    // 免分 P 形态);invalid → 失效稿件,主体不可起播(占位标题由宿主页替换)
    readonly property double epIdValue: cardItem.epId ? cardItem.epId : 0
    readonly property double cidValue: cardItem.cid ? cardItem.cid : 0
    readonly property bool invalidEntry: cardItem.invalid === true
    // 动态域扩展键(DynamicsController 注入;历史/稍后条目无此键,语义不变):
    // duplicateCount/duplicateTimes → 同一 av 号在动态流中的多次出现(去重合并
    // 角标,与多次观看角标同槽互斥 —— 动态条目 viewCount 恒 0);dynamicUnavailable →
    // 失效动态占位(无封面/无标题,保留时间戳,不可点击跳转)
    readonly property int duplicateCountValue: cardItem.duplicateCount ? cardItem.duplicateCount : 0
    readonly property var duplicateTimes: cardItem.duplicateTimes || []
    readonly property bool dynamicUnavailable: cardItem.dynamicUnavailable === true

    // 进度 0 与"已看完"(B 站 progress=-1)都不显示进度条/进度文本
    readonly property double progressFraction:
        progressValue > 0 && durationValue > 0
            ? Math.min(1.0, progressValue / durationValue) : 0.0
    // 播放资格:archive 按 aid；PGC 有 epId 即可，缺失 cid 由播放器加载分集详情。
    // B 站 aid/cid 已可超过 32 位有符号范围，必须用 double 保留完整整数。
    readonly property bool videoEntry: business === "archive" || business === "pgc"
    readonly property bool playable: playbackEnabled && !invalidEntry && !dynamicUnavailable &&
        (playbackOverride || (business === "archive" && oidValue > 0) ||
         (business === "pgc" && (epIdValue > 0 ||
             (seasonPlaybackEnabled && Number(cardItem.seasonId || 0) > 0))))

    // 原图预览地址沿用原有解码兼容口径，独立于卡片缩略图。
    readonly property string baseUrl: Format.ensureDecodableImageUrl(
            Format.stripImageTranscode(coverUrl), AppController.decodableImageFormats)
    readonly property string thumbnailUrl: Format.cardCoverThumbnailUrl(coverUrl)

    readonly property string watchedAtText: {
        var formatted = Format.formatTimestamp(viewAtValue)
        return formatted === "" ? qsTr("未知时间") : formatted
    }

    // 业务类型标签:固定映射(视频/番剧影视/直播/专栏/文集),其他回退 badge
    function businessLabel() {
        switch (business) {
            case "local": return qsTr("本地媒体")
            case "archive": return qsTr("视频")
            case "pgc": return qsTr("番剧/影视")
            case "live": return qsTr("直播")
            case "article": return qsTr("专栏")
            case "article-list": return qsTr("文集")
            default: return badgeText !== "" ? badgeText : qsTr("历史")
        }
    }

    // 多次观看 tooltip:"共 N 次观看:1. …"(时间倒序,逐行)
    readonly property string viewTimestampsTooltip: {
        var saved = cardItem.recordedViews || []
        if (showRecordedBadge && saved.length > 0) {
            var savedLines = []
            for (var j = 0; j < saved.length; ++j) {
                var position = saved[j].progress < 0 ? qsTr("已看完") : Format.formatSeconds(saved[j].progress)
                savedLines.push((j + 1) + ". " + Format.formatTimestamp(saved[j].viewAt) + " · " + position)
            }
            return qsTr("共 %1 次观看:%2").arg(String(viewCountValue)).arg("\n" + savedLines.join("\n"))
        }
        var records = viewRecords
        if (!records || records.length === 0) return watchedAtText
        var sorted = records.slice().sort(function (a, b) { return b - a })
        var lines = []
        for (var i = 0; i < sorted.length; i++) {
            lines.push((i + 1) + ". " + Format.formatTimestamp(sorted[i]))
        }
        return qsTr("共 %1 次观看:%2").arg(String(viewCountValue)).arg(lines.join("\n"))
    }

    // 多次出现 tooltip:"该视频出现 N 次:1. 时间 作者 投稿/转发"(发布时间倒序)
    readonly property string duplicateTooltip: {
        var records = duplicateTimes
        if (!records || records.length === 0) return watchedAtText
        var sorted = records.slice().sort(function (a, b) {
            return (b.pubTs ? b.pubTs : 0) - (a.pubTs ? a.pubTs : 0)
        })
        var lines = []
        for (var i = 0; i < sorted.length; i++) {
            var record = sorted[i]
            var form = record.isForward === true ? qsTr("转发") : qsTr("投稿")
            lines.push((i + 1) + ". " + Format.formatTimestamp(record.pubTs ? record.pubTs : 0) +
                       " " + String(record.authorName || "") + " " + form)
        }
        return qsTr("该视频出现 %1 次:%2").arg(String(duplicateCountValue)).arg(lines.join("\n"))
    }

    function playInNewWindow() {
        if (!playable) return
        if (playbackOverride) {
            playbackRequested(cardItem)
            return
        }
        if (seasonPlaybackEnabled && business === "pgc" && Number(cardItem.seasonId || 0) > 0) {
            seasonPlayRequested(cardItem)
            return
        }
        PlayerController.openWith([{
            videoKey: String(cardItem.videoKey || ""),
            title: titleText,
            subtitle: displaySubtitle,
            authorName: String(cardItem.authorName || ""),
            coverUrl: coverUrl,
            business: business,
            oid: oidValue,
            kid: cardItem.kid ? cardItem.kid : 0,
            // PGC 分集定位(稍后再看条目注入;历史条目为 0,archive 路径不读)
            epId: epIdValue,
            cid: cidValue,
            duration: durationValue,
            progress: progressValue,
            rawJson: String(cardItem.rawJson || ""),
            linkUrl: linkUrl
        }])
        FluRouter.navigate("/player")
    }

    function openAuthor() {
        if (!authorNavigable) return
        authorClicked({mid: authorMid, name: String(cardItem.authorName || ""),
                          faceUrl: String(cardItem.faceUrl || "")})
    }

    function openContextMenu() {
        if (contextMenuOverride) contextMenuRequested(card_root)
        else card_menu.popup()
    }

    FluFrame {
        id: card_frame

        anchors.fill: parent
        radius: 8
    }

    // 卡片主体点击层:位于封面层之下 —— 封面自己的 MouseArea(仅左键)优先;
    // 右键不被封面接受,穿透到这里弹菜单
    MouseArea {
        id: card_mouse

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: card_root.playable ||
                     (!card_root.videoEntry && !card_root.dynamicUnavailable &&
                      card_root.linkUrl !== "") ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                card_root.openContextMenu()
            } else if (card_root.playable) {
                card_root.playInNewWindow()
            } else if (!card_root.videoEntry && !card_root.dynamicUnavailable &&
                       card_root.linkUrl !== "") {
                Qt.openUrlExternally(card_root.linkUrl)
            }
        }
    }

    Column {
        id: content_column

        width: card_root.width

        // ---- 封面区(16:9;失效动态占位时整体隐藏) ----
        Item {
            id: cover_area

            width: parent.width
            height: card_root.dynamicUnavailable ? 0 : Math.round(width * 9 / 16)
            visible: !card_root.dynamicUnavailable

            FluImage {
                id: cover_image

                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true  // 固定尺寸 URL 可跨卡片及页面复用已加载图片
                sourceSize: Qt.size(400, 225)  // Bound decoded local/original covers too.
                source: card_root.thumbnailUrl
                visible: card_root.coverUrl !== ""
            }
            FluIcon {
                anchors.centerIn: parent
                visible: card_root.coverUrl === ""
                iconSource: FluentIcons.Play
                iconSize: 40
                iconColor: FluTheme.fontSecondaryColor
            }

            // 时长角标(右下,live/article 无时长不显)
            Rectangle {
                id: duration_badge

                visible: card_root.durationValue > 0
                radius: 3
                color: "#B0000000"
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: 6
                    bottomMargin: 8
                }
                width: duration_text.implicitWidth + 10
                height: duration_text.implicitHeight + 2
                FluText {
                    id: duration_text

                    anchors.centerIn: parent
                    text: Format.formatSeconds(card_root.durationValue)
                    font.pixelSize: 11
                    textColor: "white"
                }
            }

            // 多次观看角标(viewCount>1,右上,粉底)悬停列全部观看时间
            Rectangle {
                id: multi_badge
                objectName: "historyRecordedBadge"

                visible: card_root.viewCountValue > 1 || (card_root.showRecordedBadge && card_root.viewCountValue > 0)
                radius: 10
                color: "#E6FB7299"
                width: multi_badge_row.implicitWidth + 12
                height: multi_badge_row.implicitHeight + 4
                anchors {
                    right: parent.right
                    top: parent.top
                    rightMargin: 8
                    topMargin: 8
                }
                Row {
                    id: multi_badge_row

                    spacing: 3
                    anchors.centerIn: parent
                    FluIcon {
                        iconSource: FluentIcons.History
                        iconSize: 10
                        iconColor: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FluText {
                        text: card_root.showRecordedBadge ? qsTr("已记录 · %1 次").arg(String(card_root.viewCountValue))
                                                        : qsTr("%1 次").arg(String(card_root.viewCountValue))
                        font.pixelSize: 11
                        textColor: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler {
                    id: badge_hover
                }
                FluTooltip {
                    id: badge_tooltip

                    text: card_root.viewTimestampsTooltip
                    visible: badge_hover.hovered
                    delay: 300
                    x: parent.width - implicitWidth
                    y: parent.y + parent.height + 6
                }
            }

            // 同视频多次出现角标(duplicateCount>1,右上同槽;与多次观看角标互斥)
            Rectangle {
                id: duplicate_badge

                visible: card_root.duplicateCountValue > 1 && card_root.viewCountValue <= 1
                radius: 10
                color: "#E6FB7299"
                width: duplicate_badge_row.implicitWidth + 12
                height: duplicate_badge_row.implicitHeight + 4
                anchors {
                    right: parent.right
                    top: parent.top
                    rightMargin: 8
                    topMargin: 8
                }
                Row {
                    id: duplicate_badge_row

                    spacing: 3
                    anchors.centerIn: parent
                    FluIcon {
                        iconSource: FluentIcons.History
                        iconSize: 10
                        iconColor: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    FluText {
                        text: qsTr("出现 %1 次").arg(String(card_root.duplicateCountValue))
                        font.pixelSize: 11
                        textColor: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler {
                    id: duplicate_hover
                }
                FluTooltip {
                    id: duplicate_tooltip

                    text: card_root.duplicateTooltip
                    visible: duplicate_hover.hovered
                    delay: 300
                    x: parent.width - implicitWidth
                    y: parent.y + parent.height + 6
                }
            }

            // 观看进度条(底部;0 或已看完不显)
            Item {
                id: progress_track

                visible: card_root.progressFraction > 0
                width: parent.width
                height: 4
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                Rectangle {
                    anchors.fill: parent
                    color: "#40FFFFFF"
                }
                Rectangle {
                    width: parent.width * card_root.progressFraction
                    height: parent.height
                    color: "#FF6699"
                }
            }

            // 封面点击:只开预览,不触发主体起播或跳转(互斥契约)
            MouseArea {
                id: cover_mouse

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                onClicked: card_root.coverClicked(cover_area)
            }
        }

        // ---- 信息区(内边距 12,10,12,12,对齐原型) ----
        Item {
            id: text_block

            width: parent.width
            height: text_inner.height + 22
            Column {
                id: text_inner

                x: 12
                y: 10
                width: parent.width - 24
                spacing: 6

                FluText {
                    width: parent.width
                    // 失效动态:标题隐藏,呈"动态已失效"标签(保留时间戳/作者行)
                    text: card_root.dynamicUnavailable ? qsTr("动态已失效") : card_root.titleText
                    font: FluTextStyle.BodyStrong
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                FluText {
                    visible: card_root.displaySubtitle !== "" && !card_root.dynamicUnavailable
                    width: parent.width
                    text: card_root.displaySubtitle
                    font: FluTextStyle.Caption
                    textColor: FluTheme.fontSecondaryColor
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                FocusScope {
                    id: author_button
                    objectName: "cardAuthorButton"
                    visible: card_root.showAuthor

                    width: parent.width
                    height: card_root.authorNavigable ? 26 : author_row.implicitHeight
                    activeFocusOnTab: card_root.authorNavigable
                    readonly property bool highlighted: card_root.authorNavigable &&
                            (author_mouse.containsMouse || author_mouse.pressed || activeFocus)
                    Accessible.role: card_root.authorNavigable ? Accessible.Link : Accessible.StaticText
                    Accessible.name: card_root.authorNavigable
                            ? qsTr("打开 %1 的个人空间").arg(card_root.authorLine) : card_root.authorLine
                    Accessible.onPressAction: card_root.openAuthor()
                    Keys.onPressed: function(event) {
                        if (card_root.authorNavigable &&
                                (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                                 event.key === Qt.Key_Space)) {
                            event.accepted = true
                            if (!event.isAutoRepeat) card_root.openAuthor()
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: author_mouse.pressed ? FluTheme.itemPressColor :
                               author_button.highlighted ? FluTheme.itemHoverColor : "transparent"
                        border.width: author_button.activeFocus && card_root.authorNavigable ? 1 : 0
                        border.color: FluTheme.primaryColor
                    }
                    Row {
                        id: author_row

                        anchors {
                            left: parent.left
                            right: parent.right
                            margins: card_root.authorNavigable ? 6 : 0
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 6
                        FluIcon {
                            iconSource: FluentIcons.Contact
                            iconSize: 12
                            iconColor: author_button.highlighted ? FluTheme.primaryColor :
                                                                  FluTheme.fontSecondaryColor
                            opacity: author_button.highlighted ? 1 : 0.6
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        FluText {
                            text: card_root.authorLine
                            font.pixelSize: 13
                            textColor: author_button.highlighted ? FluTheme.primaryColor : FluTheme.fontPrimaryColor
                            opacity: author_button.highlighted ? 1 : 0.82
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, parent.width - 18)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: author_mouse

                        anchors.fill: parent
                        enabled: card_root.authorNavigable
                        hoverEnabled: true
                        // 作者行消费右键，避免误打开视频主体的菜单。
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            if (mouse.button === Qt.LeftButton)
                                author_button.forceActiveFocus(Qt.MouseFocusReason)
                        }
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.LeftButton) card_root.openAuthor()
                        }
                    }
                }
                Row {
                    spacing: 8
                    Rectangle {
                        radius: 3
                        color: FluTheme.itemHoverColor
                        width: business_chip_text.implicitWidth + 12
                        height: business_chip_text.implicitHeight + 2
                        anchors.verticalCenter: parent.verticalCenter
                        FluText {
                            id: business_chip_text

                            anchors.centerIn: parent
                            text: card_root.businessLabel()
                            font: FluTextStyle.Caption
                            opacity: 0.9
                        }
                    }
                    FluText {
                        visible: card_root.showHistoryTime
                        text: card_root.watchedAtText
                        font: FluTextStyle.Caption
                        opacity: 0.6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                FluText {
                    visible: card_root.statisticsText !== ""
                    width: parent.width
                    text: card_root.statisticsText
                    font: FluTextStyle.Caption
                    textColor: FluTheme.fontSecondaryColor
                    elide: Text.ElideRight
                }
                FluText {
                    visible: card_root.progressValue > 0 && card_root.durationValue > 0
                    text: Format.formatSeconds(card_root.progressValue) + " / " +
                          Format.formatSeconds(card_root.durationValue)
                    font: FluTextStyle.Caption
                    opacity: 0.6
                }
            }
        }
    }

    FluMenu {
        id: card_menu
        objectName: "cardContextMenu"

        FluMenuItem {
            text: qsTr("下载")
            visible: card_root.downloadEnabled && card_root.videoEntry
            enabled: card_root.playable
            onClicked: DownloadController.requestDownload(card_root.cardItem)
        }
        FluMenuItem {
            text: qsTr("打开链接")
            enabled: card_root.linkUrl !== ""
            onClicked: Qt.openUrlExternally(card_root.linkUrl)
        }
    }
}
