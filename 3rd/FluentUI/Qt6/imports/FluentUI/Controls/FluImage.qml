import QtQuick
import QtQuick.Window
import FluentUI

Image {
    property string errorButtonText: qsTr("Reload")
    property var clickErrorListener : function(){
        // 原实现引用不存在的 id(image),重试按钮点击必抛 ReferenceError;
        // 清空与回写分帧执行,避免同帧内被引擎合并为"无变化"而不触发重载
        var previous = control.source
        control.source = ""
        Qt.callLater(function(){ control.source = previous })
    }
    property Component errorItem : com_error
    property Component loadingItem: com_loading
    id: control

    // 网络图为绝对主流用例:默认异步解码,避免阻塞 UI 线程
    asynchronous: true
    // 解码尺寸钳制:超过 显示尺寸×dpr(上限2x) 无视觉收益,B 站 4K 封面全量
    // 解码极易爆内存;需要原图语义的调用方(如原图预览)应显式覆盖 sourceSize
    sourceSize: (width > 0 && height > 0)
                ? Qt.size(Math.ceil(width * Math.min(Screen.devicePixelRatio, 2)),
                          Math.ceil(height * Math.min(Screen.devicePixelRatio, 2)))
                : undefined

    FluLoader{
        anchors.fill: parent
        sourceComponent: {
            if(control.status === Image.Loading){
                return control.loadingItem
            }else if(control.status == Image.Error){
                return control.errorItem
            }else{
                return undefined
            }
        }
    }
    Component{
        id:com_loading
        Rectangle{
            color: FluTheme.itemHoverColor
            FluProgressRing{
                anchors.centerIn: parent
                visible: control.status === Image.Loading
            }
        }
    }
    Component{
        id:com_error
        Rectangle{
            color: FluTheme.itemHoverColor
            FluFilledButton{
                text: control.errorButtonText
                anchors.centerIn: parent
                visible: control.status === Image.Error
                onClicked: clickErrorListener()
            }
        }
    }
}
