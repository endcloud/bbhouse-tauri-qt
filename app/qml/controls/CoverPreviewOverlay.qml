import QtQuick
import FluentUI

// 封面原图预览层(history-browser-ui 封面预览契约):
// - 全屏遮罩 + 原图(URL 由调用方去掉 '@…' 转码后缀)
// - 左键单击任意位置(遮罩或大图)关闭;无独立关闭按钮
// - 右键菜单"下载原图到图片文件夹":经 HistoryController.coverDownload
//   (带 Referer/UA,保存到 系统图片目录/bilibili_cover),成功/失败 InfoBar 反馈
// - 打开/关闭播放缩放过渡:大图从来源卡片封面矩形展开至全屏/缩回来源位置
//   (等价原 WinUI ConnectedAnimation);来源缺失或动画不可用时直接显隐
// 使用方式:宿主页 anchors.fill 并置于最高 z(须盖住回到顶部按钮)。
Item {
    id: overlay_root

    // 当前预览的原图 URL(去转码后缀)
    property string previewUrl: ""
    // 打开来源(卡片封面)在 overlay 坐标系的矩形 {x,y,width,height};null = 无来源
    property var sourceRect: null

    visible: false

    function fitRect() {
        var margin = 24
        return {
            x: margin,
            y: margin,
            width: Math.max(1, overlay_root.width - 2 * margin),
            height: Math.max(1, overlay_root.height - 2 * margin)
        }
    }

    function refit() {
        if (!visible || zoom_out_anim.running) return
        zoom_in_anim.stop()
        applyRect(fitRect())
    }
    onWidthChanged: refit()
    onHeightChanged: refit()

    function applyRect(rect) {
        preview_image.x = rect.x
        preview_image.y = rect.y
        preview_image.width = rect.width
        preview_image.height = rect.height
    }

    // 函数式打开:show(原图 URL, 来源 item 可空)
    function show(originalUrl, sourceItem) {
        var url = originalUrl ? String(originalUrl) : ""
        if (url.trim() === "") return
        zoom_out_anim.stop()
        previewUrl = url
        preview_image.source = url
        if (sourceItem) {
            try {
                var mapped = sourceItem.mapToItem(overlay_root, 0, 0)
                sourceRect = {
                    x: mapped.x, y: mapped.y,
                    width: sourceItem.width, height: sourceItem.height
                }
            } catch (e) {
                sourceRect = null
            }
        } else {
            sourceRect = null
        }
        overlay_root.visible = true
        if (sourceRect) {
            applyRect(sourceRect)
            var target = fitRect()
            anim_x.from = sourceRect.x;   anim_x.to = target.x
            anim_y.from = sourceRect.y;   anim_y.to = target.y
            anim_w.from = sourceRect.width; anim_w.to = target.width
            anim_h.from = sourceRect.height; anim_h.to = target.height
            zoom_in_anim.restart()
        } else {
            zoom_in_anim.stop()
            applyRect(fitRect())
        }
    }

    function close() {
        if (!overlay_root.visible) return
        if (sourceRect) {
            zoom_in_anim.stop()
            var target = fitRect()
            out_x.from = target.x;     out_x.to = sourceRect.x
            out_y.from = target.y;     out_y.to = sourceRect.y
            out_w.from = target.width; out_w.to = sourceRect.width
            out_h.from = target.height; out_h.to = sourceRect.height
            zoom_out_anim.restart()
        } else {
            overlay_root.visible = false
        }
    }

    Rectangle {
        id: mask_rect

        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.65)
    }

    FluImage {
        id: preview_image

        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
    }

    // 打开:来源矩形 → 全屏
    ParallelAnimation {
        id: zoom_in_anim

        NumberAnimation { id: anim_x; target: preview_image; property: "x"; duration: 220; easing.type: Easing.OutCubic }
        NumberAnimation { id: anim_y; target: preview_image; property: "y"; duration: 220; easing.type: Easing.OutCubic }
        NumberAnimation { id: anim_w; target: preview_image; property: "width"; duration: 220; easing.type: Easing.OutCubic }
        NumberAnimation { id: anim_h; target: preview_image; property: "height"; duration: 220; easing.type: Easing.OutCubic }
    }

    // 关闭:全屏 → 来源矩形,完成后隐藏整层(隐藏挂在动画序列尾部,
    // restart/stop 中断时不会误触发)
    SequentialAnimation {
        id: zoom_out_anim

        ParallelAnimation {
            NumberAnimation { id: out_x; target: preview_image; property: "x"; duration: 220; easing.type: Easing.InCubic }
            NumberAnimation { id: out_y; target: preview_image; property: "y"; duration: 220; easing.type: Easing.InCubic }
            NumberAnimation { id: out_w; target: preview_image; property: "width"; duration: 220; easing.type: Easing.InCubic }
            NumberAnimation { id: out_h; target: preview_image; property: "height"; duration: 220; easing.type: Easing.InCubic }
        }
        ScriptAction {
            script: overlay_root.visible = false
        }
    }

    FluMenu {
        id: preview_menu

        FluMenuItem {
            text: qsTr("下载原图到图片文件夹")
            enabled: overlay_root.previewUrl !== ""
            onClicked: HistoryController.coverDownload(overlay_root.previewUrl)
        }
    }

    // 遮罩/大图统一交互:左键任意处关闭;右键弹下载原图菜单
    MouseArea {
        id: overlay_mouse

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                preview_menu.popup()
            } else {
                overlay_root.close()
            }
        }
    }

    FluInfoBar {
        id: preview_info_bar

        root: overlay_root
    }

    Connections {
        target: HistoryController

        function onCoverDownloadFinished(savedPath) {
            if (!overlay_root.visible) return
            if (savedPath && savedPath.length > 0) {
                preview_info_bar.showSuccess(qsTr("已保存到 %1").arg(savedPath), 4000)
            } else {
                preview_info_bar.showError(qsTr("下载失败"), 4000)
            }
        }
    }
}
