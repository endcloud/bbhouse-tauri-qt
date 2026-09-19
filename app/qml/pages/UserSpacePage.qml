import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import bbhouse
import "../js/Format.js" as Format

// 个人空间独占控制器会话；特别关注名单仅供本地收藏按钮读取和保存。
FluPage {
    id: page
    padding: 0
    signal backRequested()
    property alias searchQuery: content.searchQuery
    readonly property bool compactHeader: width < 680
    readonly property bool followed: {
        var members = SpecialFollowController.ups
        return SpecialFollowController.containsUp(UserSpaceController.profileMid)
    }
    readonly property string displayName: UserSpaceController.profileName !== ""
            ? UserSpaceController.profileName : qsTr("个人空间")

    function resetView() { content.resetView() }

    function refresh() {
        UserSpaceController.refreshProfile()
        content.refreshCurrentTab()
    }

    FluInfoBar { id: info_bar; root: page }

    Component {
        id: avatar_placeholder
        Image {
            source: "qrc:/images/noface.jpg"
            fillMode: Image.PreserveAspectCrop
            mipmap: true
        }
    }

    Item {
        id: profile_header
        height: page.compactHeader ? 126 : 88
        anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24 }

        RowLayout {
            id: identity_row
            height: 72
            anchors {
                top: parent.top
                topMargin: 8
                left: parent.left
                right: page.compactHeader ? parent.right : profile_actions.left
                rightMargin: page.compactHeader ? 0 : 16
            }
            spacing: 12

            FluIconButton {
                id: back_button
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 17
                iconSource: FluentIcons.ChevronLeft
                iconSize: 14
                Accessible.name: qsTr("返回上一页")
                onClicked: page.backRequested()
                FluTooltip {
                    text: qsTr("返回上一页")
                    visible: back_button.hovered
                    delay: 300
                }
            }

            FluClip {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: [28, 28, 28, 28]
                FluImage {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    loadingItem: avatar_placeholder
                    errorItem: avatar_placeholder
                    source: profile_avatar.source.toString() !== ""
                            ? profile_avatar.source : "qrc:/images/noface.jpg"
                    AvatarSource {
                        id: profile_avatar
                        userId: UserSpaceController.profileMid
                        remoteUrl: Format.ensureDecodableImageUrl(
                                UserSpaceController.profileFace,
                                AppController.decodableImageFormats)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3
                FluText {
                    Layout.fillWidth: true
                    text: page.displayName
                    font.pixelSize: 20
                    font.bold: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
                FluText {
                    Layout.fillWidth: true
                    text: qsTr("UID：%1").arg(UserSpaceController.profileMid)
                    font: FluTextStyle.Caption
                    textColor: FluTheme.fontSecondaryColor
                    elide: Text.ElideRight
                }
                FluText {
                    Layout.fillWidth: true
                    text: UserSpaceController.profileBusy ? qsTr("正在加载空间资料...")
                          : (UserSpaceController.profileError !== "" ? qsTr("投稿视频")
                             : qsTr("共 %1 个投稿视频").arg(content.formatNumber(UserSpaceController.profileArchiveCount)))
                    font: FluTextStyle.Caption
                    textColor: FluTheme.fontSecondaryColor
                    elide: Text.ElideRight
                }
            }
        }

        Row {
            id: profile_actions
            anchors {
                right: parent.right
                top: page.compactHeader ? identity_row.bottom : parent.top
                topMargin: page.compactHeader ? 8 : 28
            }
            spacing: 12
            FluFilledButton {
                id: follow_button
                text: page.followed ? qsTr("已特别关注") : qsTr("设为特别关注")
                height: 34
                enabled: UserSpaceController.currentMid > 0 && SpecialFollowController.upsReady
                         && !SpecialFollowController.localFollowBusy
                onClicked: SpecialFollowController.setUpFollowed(
                        UserSpaceController.profileMid,
                        UserSpaceController.profileName,
                        UserSpaceController.profileFace,
                        !page.followed)
                FluTooltip {
                    text: page.followed ? qsTr("取消特别关注") : qsTr("添加到本地特别关注")
                    visible: follow_button.hovered
                    delay: 300
                }
            }
            FluIconButton {
                id: refresh_button
                width: 34
                height: 34
                radius: 17
                iconSource: FluentIcons.Refresh
                iconSize: 14
                enabled: UserSpaceController.currentMid > 0 && !UserSpaceController.profileBusy && !content.tabBusy
                Accessible.name: qsTr("刷新个人空间")
                onClicked: page.refresh()
                FluTooltip {
                    text: qsTr("刷新个人空间")
                    visible: refresh_button.hovered
                    delay: 300
                }
            }
        }
    }

    FluText {
        id: profile_error
        anchors { top: profile_header.bottom; left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24 }
        visible: UserSpaceController.profileError !== ""
        height: visible ? implicitHeight + 12 : 0
        text: qsTr("空间资料加载失败：%1。可点击刷新重试。")
              .arg(UserSpaceController.profileError)
        wrapMode: Text.Wrap
        font: FluTextStyle.Caption
        textColor: FluTheme.fontSecondaryColor
    }

    SpecialFollowPage {
        id: content
        controller: UserSpaceController
        spaceMode: true
        anchors { top: profile_error.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
    }

    Connections {
        target: SpecialFollowController
        enabled: page.visible
        function onLoadFailed(message) { info_bar.showError(message, 4000) }
    }
    Component.onCompleted: SpecialFollowController.ensureLocalReady()
}
