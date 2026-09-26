import QtQuick
import FluentUI
import "../js/Format.js" as Format

// 竖版封面卡片(bangumi-ui 卡片视觉契约,为本页重新实现,不复用横版 HistoryCard):
// 竖版封面(170×226 ≈ 3:4)为主体 + 封面右下角评分角标(rating.score,无评分省略)+
// 封面下标题(单行截断)与副标(单行截断、次级色:观看进度文本优先,否则条目简介,
// 均为空省略)。封面只转 WebP，保留原图尺寸和比例，加载失败显示重试。
// 交互:左键 → cardClicked(交宿主起播剧集,
// 不打开浏览器);右键 → detailRequested(详情浮层)。宽度定宽 170(等宽网格契约)。
Item {
    id: card_root

    property var cardItem: ({})
    signal cardClicked(var cardItem)
    signal detailRequested(var cardItem)

    width: 170
    implicitHeight: Math.ceil(content_column.height + 10)

    // ---- 字段快捷口径(BangumiController::toItemMap 键名) ----
    readonly property string titleText: String(cardItem.title || "")
    // 副标:观看进度文本优先,否则条目简介,均为空省略副标行
    readonly property string subtitleText: {
        var progress = String(cardItem.progressText || "").trim()
        if (progress !== "") return progress
        return String(cardItem.evaluate || "").trim()
    }
    // 竖版封面:cover 优先(横版存储时的竖版转码),缺失回退 squareCover
    readonly property string coverUrl: String(cardItem.cover || cardItem.squareCover || "")
    readonly property bool ratingVisible: cardItem.ratingAvailable === true
    readonly property double ratingScore: cardItem.ratingScore ? cardItem.ratingScore : 0

    // 原图地址仅用于详情/预览，卡片转码不更改模型。
    readonly property string baseUrl: Format.ensureDecodableImageUrl(
            Format.stripImageTranscode(coverUrl), AppController.decodableImageFormats)
    readonly property string thumbnailUrl: Format.seasonCoverUrl(coverUrl)

    Rectangle {
        id: card_background

        anchors.fill: parent
        radius: 8
        color: card_mouse.containsMouse ? FluTheme.itemHoverColor : Qt.rgba(0, 0, 0, 0)
        border.color: card_mouse.containsMouse ? FluTheme.primaryColor : Qt.rgba(0, 0, 0, 0)
        border.width: 1
    }

    MouseArea {
        id: card_mouse

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                card_root.detailRequested(card_root.cardItem)
            } else {
                card_root.cardClicked(card_root.cardItem)
            }
        }
    }

    Column {
        id: content_column

        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: 4
        }

        // ---- 竖版封面区(170×226,约 3:4) ----
        Item {
            id: cover_area

            width: parent.width
            height: Math.round(width * 226 / 170)

            FluImage {
                id: cover_image
                objectName: "seasonCoverImage"

                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                source: card_root.thumbnailUrl
                // 与 FluImage 默认策略一致：解码 DPR 上限 2，避免高 DPI 放大纹理预算。
                sourceSize: Qt.size(width * Math.min(Screen.devicePixelRatio, 2),
                                   height * Math.min(Screen.devicePixelRatio, 2))
            }

            // 评分角标(封面右下角;条目无评分省略)
            Rectangle {
                id: rating_badge

                visible: card_root.ratingVisible
                radius: 3
                color: "#F0FFB027"
                width: rating_text.implicitWidth + 10
                height: rating_text.implicitHeight + 2
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    rightMargin: 6
                    bottomMargin: 8
                }
                FluText {
                    id: rating_text

                    anchors.centerIn: parent
                    text: card_root.ratingScore.toFixed(1)
                    font.pixelSize: 11
                    font.bold: true
                    textColor: "white"
                }
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("评分 %1").arg(card_root.ratingScore.toFixed(1))
            }
        }

        // ---- 信息区:标题单行 + 副标单行(空则省略) ----
        Column {
            id: info_column

            width: parent.width - 8
            x: 4
            topPadding: 8
            spacing: 4

            FluText {
                width: parent.width
                text: card_root.titleText
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.NoWrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }
            FluText {
                visible: card_root.subtitleText !== ""
                width: parent.width
                text: card_root.subtitleText
                font: FluTextStyle.Caption
                textColor: FluTheme.fontSecondaryColor
                wrapMode: Text.NoWrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }
    }
}
