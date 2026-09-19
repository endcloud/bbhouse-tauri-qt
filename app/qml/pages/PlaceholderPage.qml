import QtQuick
import FluentUI

// 占位页(动态/特别关注/稍后再看/在线历史共用骨架,本变更阶段):
// 冻结标题带规格与真实页面一致(高 56 / 水平边距 24 / 垂直居中)。
// 注:app-navigation-shell 规约的终态是全部菜单指向真实页面,占位控件仅作
// 工程基建保留,待各 UI 变更接入后替换。
FluPage {
    id: page
    // 页面自行管理 24px 外边距，抵消 FluPage 默认的额外 5px padding。
    padding: 0

    property string pageTitle: qsTr("占位")

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
            text: page.pageTitle
            font: FluTextStyle.Title
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
        }
    }

    FluText {
        anchors.centerIn: parent
        text: qsTr("页面内容将在后续变更实现")
    }
}
