import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

FluPage {
    id: page
    padding: 0
    property var navigationState: null
    property bool stateReady: false
    function restoreScroll() {
        if (stateReady || scroll_view.height <= 0 || scroll_view.contentHeight <= 0) return
        var saved = navigationState ? Number(navigationState.value.scrollOffset || 0) : 0
        scroll_view.contentY = Math.max(0, Math.min(saved, scroll_view.contentHeight - scroll_view.height))
        stateReady = true
    }
    Component.onCompleted: Qt.callLater(restoreScroll)
    Component.onDestruction: {
        if (navigationState && stateReady) navigationState.value = {scrollOffset: Math.max(0, scroll_view.contentY)}
    }
    readonly property string repositoryUrl: "https://github.com/endcloud/bbhouse-tauri-qt"
    // 与 THIRD_PARTY_NOTICES.md 对应；依赖和设计参考均逐项保留来源。
    readonly property var projects: [
        { name: "Qt 6", description: qsTr("跨平台应用开发框架，提供本项目的界面、网络和数据库能力。"), url: "https://github.com/qt", license: qsTr("LGPL / GPL（按模块）") },
        { name: "FluentUI", description: qsTr("为 Qt Quick 提供 Fluent Design 风格的桌面控件。"), url: "https://github.com/zhuzichu520/FluentUI", license: "MIT" },
        { name: "libqrencode", description: qsTr("将文本编码为二维码的 C 语言库。"), url: "https://github.com/fukuchi/libqrencode", license: "LGPL-2.1-or-later" },
        { name: "QHotkey", description: qsTr("为 Qt 应用提供跨平台全局快捷键支持。"), url: "https://github.com/Skycoder42/QHotkey", license: "BSD-3-Clause" },
        { name: "QCustomPlot", description: qsTr("用于 Qt 应用的交互式绘图与数据可视化控件。"), url: "https://www.qcustomplot.com/", license: "GPL-3.0-or-later" },
        { name: "Chart.js", description: qsTr("基于 Canvas 的 JavaScript 图表库。"), url: "https://github.com/chartjs/Chart.js", license: "MIT" },
        { name: "ChartJs2QML", description: qsTr("将 Chart.js 图表带入 Qt Quick 的适配组件。"), url: "https://github.com/Elypson/ChartJs2QML", license: "MIT" },
        { name: "color", description: qsTr("提供颜色解析、转换和操作的 JavaScript 库。"), url: "https://github.com/Qix-/color", license: "MIT" },
        { name: "color-convert", description: qsTr("提供多种颜色空间之间的转换。"), url: "https://github.com/Qix-/color-convert", license: "MIT" },
        { name: "color-name", description: qsTr("提供 CSS 颜色名称到 RGB 数值的映射。"), url: "https://github.com/colorjs/color-name", license: "MIT" },
        { name: "color-string", description: qsTr("解析并生成 CSS 颜色字符串。"), url: "https://github.com/Qix-/color-string", license: "MIT" },
        { name: "mpv / libmpv", description: qsTr("跨平台媒体播放器及可嵌入的音视频播放内核。"), url: "https://github.com/mpv-player/mpv", license: qsTr("GPL / LGPL（按构建）；API headers: ISC") },
        { name: "FFmpeg", description: qsTr("音视频解码、编码、转封装和媒体处理工具集。"), url: "https://github.com/FFmpeg/FFmpeg", license: qsTr("LGPL / GPL（按构建）") },
        { name: "aria2", description: qsTr("支持多协议、多连接的命令行下载工具。"), url: "https://github.com/aria2/aria2", license: "GPL-2.0-or-later" },
        { name: "curl", description: qsTr("支持多种网络协议的命令行数据传输工具。"), url: "https://github.com/curl/curl", license: "curl license" },
        { name: "SQLite", description: qsTr("无需独立服务的嵌入式 SQL 数据库引擎。"), url: "https://sqlite.org/", license: "Public domain" },
        { name: "zlib", description: qsTr("轻量、通用的数据压缩和解压库。"), url: "https://github.com/madler/zlib", license: "zlib" },
        { name: "wiliwili", description: qsTr("面向多平台的第三方哔哩哔哩客户端。"), url: "https://github.com/xfangfang/wiliwili", license: "GPLv3" },
        { name: "bililocal", description: qsTr("支持本地视频与弹幕播放的桌面播放器。"), url: "https://github.com/ancientlysine/bililocal", license: "GPLv3" },
        { name: "pakku.js", description: qsTr("通过相似弹幕合并改善观看体验的浏览器扩展。"), url: "https://github.com/xmcp/pakku.js", license: "GPLv3" },
        { name: "Danmaku", description: qsTr("支持 DOM 和 Canvas 渲染的 JavaScript 弹幕引擎。"), url: "https://github.com/weizhenye/Danmaku", license: "MIT" },
        { name: "DanmakuFrostMaster", description: qsTr("基于 Win2D 的高性能弹幕渲染引擎。"), url: "https://github.com/cotaku/DanmakuFrostMaster", license: "MIT" },
        { name: "bilibili-API-collect", description: qsTr("整理哔哩哔哩接口、请求参数与响应结构的社区文档。"), url: "https://github.com/pskdje/bilibili-API-collect", license: "CC BY-NC 4.0" }
    ]
    FluInfoBar { id: info_bar; root: page }
    Item {
        id: title_band
        height: 56
        anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24 }
        FluText { text: qsTr("关于"); font: FluTextStyle.Title; anchors.verticalCenter: parent.verticalCenter }
    }
    Flickable {
        id: scroll_view
        objectName: "aboutScrollView"
        onHeightChanged: Qt.callLater(page.restoreScroll)
        onContentHeightChanged: Qt.callLater(page.restoreScroll)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: content.height + 24
        anchors { top: title_band.bottom; topMargin: 8; bottom: parent.bottom; left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24 }
        Column {
            id: content
            width: parent.width - 12
            spacing: 16
            FluFrame {
                width: parent.width
                height: app_info.implicitHeight + 32
                ColumnLayout {
                    id: app_info
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Image {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            source: Qt.platform.os === "osx"
                                    ? "qrc:/icons/bbhouse-icon-1024-mac.png"
                                    : "qrc:/icons/bbhouse-icon-1024.png"
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            FluText { text: qsTr("BBHouse"); font: FluTextStyle.Subtitle }
                            FluText { text: qsTr("Copyright © 2026 shizi"); textColor: FluTheme.fontSecondaryColor }
                        }
                        FluText { text: qsTr("版本 %1").arg(AppController.appVersion); textColor: FluTheme.fontSecondaryColor }
                    }
                    FluText {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("基于 Qt / QML 的第三方哔哩哔哩播放器，支持历史同步、在线与本地播放。")
                    }
                    FluTextButton { text: page.repositoryUrl; onClicked: Qt.openUrlExternally(page.repositoryUrl) }
                    RowLayout {
                        Layout.fillWidth: true
                        FluCopyableText {
                            id: version_text
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("BBHouse 版本 %1(Qt 6 · FluentUI · libmpv)").arg(AppController.appVersion)
                        }
                        FluIconButton {
                            iconSource: FluentIcons.Copy
                            text: qsTr("复制版本信息")
                            onClicked: {
                                FluTools.clipText(version_text.text)
                                info_bar.showSuccess(qsTr("已复制到剪贴板"), 2500)
                            }
                        }
                    }
                }
            }
            FluText { text: qsTr("依赖与开源参考"); font: FluTextStyle.Subtitle }
            FluText {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("感谢以下项目提供基础组件、接口文档与设计参考。参考项目不代表完整捆绑依赖。")
                textColor: FluTheme.fontSecondaryColor
            }
            Repeater {
                model: page.projects
                delegate: FluFrame {
                    required property var modelData
                    width: content.width
                    height: project_row.implicitHeight + 24
                    RowLayout {
                        id: project_row
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            FluText { text: modelData.name; font: FluTextStyle.BodyStrong }
                            FluText {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: modelData.description
                                textColor: FluTheme.fontSecondaryColor
                            }
                            FluText {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: modelData.license
                                font: FluTextStyle.Caption
                                textColor: FluTheme.fontSecondaryColor
                            }
                        }
                        FluIconButton {
                            iconSource: FluentIcons.OpenInNewWindow
                            text: qsTr("打开 %1").arg(modelData.name)
                            onClicked: Qt.openUrlExternally(modelData.url)
                        }
                    }
                }
            }
            FluText { text: qsTr("法律与开源声明"); font: FluTextStyle.Subtitle }
            FluText {
                width: parent.width
                wrapMode: Text.Wrap
                textColor: FluTheme.fontSecondaryColor
                text: qsTr("本项目代码采用 GPLv3，第三方组件保留原许可证。当前内嵌微软图标字体的再分发授权尚未解决，正式发布前须处理。哔哩哔哩及创作者保留内容权利。完整归属与分发说明见仓库 THIRD_PARTY_NOTICES.md。")
            }
            FluTextButton {
                text: qsTr("完整开源归属与许可说明")
                onClicked: Qt.openUrlExternally(page.repositoryUrl + "/blob/master/THIRD_PARTY_NOTICES.md")
            }
        }
        ScrollBar.vertical: FluScrollBar {}
    }
}
