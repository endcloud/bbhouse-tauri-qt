import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import "../js/Format.js" as Format

FluPage {
    id: page
    padding: 0
    property string searchQuery: ""
    readonly property var filteredItems: {
        var query = searchQuery.trim().toLowerCase()
        return LiveController.pool.filter(function(room) {
            return !query || String(room.title).toLowerCase().indexOf(query) !== -1
                         || String(room.uname).toLowerCase().indexOf(query) !== -1
        })
    }
    onSearchQueryChanged: room_grid.positionViewAtBeginning()
    Component.onCompleted: LiveController.ensureLoaded()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            FluText { text: qsTr("直播"); font: FluTextStyle.Title }
            Item { Layout.fillWidth: true }
            FluProgressRing {
                visible: LiveController.busy
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
            }
            FluIconButton {
                iconSource: FluentIcons.Refresh
                text: qsTr("刷新关注直播")
                enabled: !LiveController.busy
                onClicked: LiveController.refresh()
            }
        }
        FluText {
            Layout.fillWidth: true
            text: qsTr("关注的正在直播 · 已加载 %1 个房间").arg(LiveController.pool.length)
            color: FluTheme.dark ? "#b9bec8" : "#596171"
            wrapMode: Text.Wrap
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: error_text.implicitHeight + 24
            radius: 8
            visible: LiveController.error.length > 0
            color: FluTheme.dark ? "#493323" : "#fff1df"
            FluText {
                id: error_text
                anchors.fill: parent
                anchors.margins: 12
                wrapMode: Text.Wrap
                text: LiveController.unauthorized
                      ? qsTr("登录已失效，请更新 Cookie 后刷新。") : LiveController.error
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridView {
                id: room_grid
                anchors.fill: parent
                clip: true
                model: page.filteredItems
                cellWidth: width / Math.max(1, Math.floor(width / 280))
                cellHeight: cellWidth * 9 / 16 + 112
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    width: room_grid.cellWidth - 12
                    height: room_grid.cellHeight - 12
                    radius: 10
                    color: FluTheme.dark ? "#272b33" : "#ffffff"
                    border.color: play_area.containsMouse || activeFocus ? FluTheme.primaryColor
                                                                        : (FluTheme.dark ? "#3c414b" : "#e1e5ec")
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("观看 %1 的直播：%2").arg(modelData.uname).arg(modelData.title)
                    Keys.onReturnPressed: play()
                    Keys.onSpacePressed: play()
                    function play() {
                        LivePlayerController.openRoom(modelData)
                        FluRouter.navigate("/live-player")
                    }
                    Rectangle {
                        id: cover
                        x: 1; y: 1
                        width: parent.width - 2
                        height: width * 9 / 16
                        radius: 9
                        color: FluTheme.dark ? "#181b22" : "#e9edf3"
                        clip: true
                        FluIcon { anchors.centerIn: parent; iconSource: FluentIcons.Webcam; iconSize: 36 }
                        Image {
                            anchors.fill: parent
                            source: Format.cardCoverThumbnailUrl(card.modelData.cover)
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize: Qt.size(400, 225)
                        }
                        Rectangle {
                            x: 10; y: 10
                            radius: 4
                            width: live_tag.implicitWidth + 16
                            height: 24
                            color: "#d82766"
                            Text { id: live_tag; anchors.centerIn: parent; text: qsTr("直播中"); color: "white"; font.pixelSize: 12 }
                        }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            width: Math.min(parent.width - 16, popularity.implicitWidth + 16)
                            height: 23
                            radius: 4
                            color: "#b3000000"
                            Text {
                                id: popularity
                                anchors.fill: parent
                                anchors.leftMargin: 8; anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                text: qsTr("人气 %1").arg(card.modelData.online || "—")
                                color: "white"; font.pixelSize: 12; elide: Text.ElideRight
                            }
                        }
                    }
                    FluText {
                        id: room_title
                        x: 12; y: cover.height + 10
                        width: parent.width - 24
                        height: 38
                        text: card.modelData.title || qsTr("直播间")
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.bold: true
                    }
                    MouseArea {
                        id: play_area
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: room_title.y + room_title.height
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: card.play()
                    }
                    RowLayout {
                        x: 10; y: room_title.y + room_title.height + 4
                        width: parent.width - 20
                        spacing: 6
                        FluTextButton {
                            Layout.fillWidth: true
                            text: card.modelData.uname || qsTr("未知主播")
                            enabled: card.modelData.mid !== ""
                            onClicked: AppController.openUserSpace(card.modelData.mid,
                                                                   card.modelData.uname,
                                                                   card.modelData.face)
                            contentItem: FluText {
                                text: parent.text
                                color: parent.textColor
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        FluText {
                            Layout.maximumWidth: card.width * 0.4
                            text: card.modelData.area
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: FluTheme.dark ? "#b9bec8" : "#596171"
                        }
                    }
                }
            }
            FluText {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 480)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: page.filteredItems.length === 0
                text: LiveController.busy ? qsTr("正在查找关注的直播…")
                    : LiveController.error ? qsTr("加载失败，请点击刷新重试。")
                    : page.searchQuery.trim() ? qsTr("已加载房间中没有匹配结果。")
                    : LiveController.hasMore ? qsTr("当前已加载的关注中无人开播，可继续加载。")
                    : qsTr("关注的主播暂未开播。")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            FluText {
                Layout.fillWidth: true
                text: qsTr("搜索仅筛选已加载房间")
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            FluButton {
                visible: LiveController.hasMore
                enabled: !LiveController.busy
                text: qsTr("加载更多")
                onClicked: LiveController.loadMore()
            }
            FluText {
                visible: LiveController.loaded && !LiveController.hasMore && !LiveController.busy
                text: qsTr("已加载全部")
            }
        }
    }
}
