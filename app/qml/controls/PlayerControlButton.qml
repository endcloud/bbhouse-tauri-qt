import QtQuick
import QtQuick.Controls
import FluentUI

// 视频覆盖层保持深色前景语义，复用 FluentUI 的按钮输入、焦点和 tooltip。
FluIconButton {
    id: control
    property string caption: ""
    property bool active: false
    property color accentColor: "#ff4d4f"

    text: caption.length > 0 ? caption : contentDescription
    display: caption.length > 0 ? Button.TextOnly : Button.IconOnly
    width: caption.length > 0 ? Math.max(48, labelMetrics.width + 20) : 36
    height: 36
    radius: 18
    iconSize: 20
    font.pixelSize: 13
    iconColor: !enabled ? "#737373" : active ? accentColor : "#f5f5f5"
    textColor: iconColor
    normalColor: "transparent"
    hoverColor: Qt.rgba(1, 1, 1, 0.12)
    pressedColor: Qt.rgba(1, 1, 1, 0.2)
    disableColor: "transparent"
    Accessible.name: contentDescription
    TextMetrics { id: labelMetrics; text: control.caption; font: control.font }
    contentItem: Item {
        implicitWidth: control.caption.length > 0 ? labelMetrics.width : control.iconSize
        implicitHeight: Math.max(control.iconSize, labelMetrics.height)
        FluIcon {
            anchors.centerIn: parent
            visible: control.caption.length === 0
            iconSource: control.iconSource
            iconSize: control.iconSize
            iconColor: control.iconColor
        }
        FluText {
            anchors.fill: parent
            visible: control.caption.length > 0
            text: control.caption
            font: control.font
            color: control.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    FluTooltip {
        visible: control.caption.length > 0 && control.hovered
        text: control.contentDescription
        delay: 700
    }
}
