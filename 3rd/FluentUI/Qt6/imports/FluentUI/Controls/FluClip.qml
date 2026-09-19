import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import FluentUI

FluRectangle {
    id:control
    color: "#00000000"
    layer.enabled: !FluTools.isSoftware()
    smooth: true
    layer.smooth: true
    layer.textureSize: Qt.size(Math.max(1, Math.ceil(control.width * 2 * Screen.devicePixelRatio)),
                               Math.max(1, Math.ceil(control.height * 2 * Screen.devicePixelRatio)))
    // Qt5Compat.GraphicalEffects.OpacityMask → QtQuick.Effects.MultiEffect 遮罩
    layer.effect: MultiEffect{
        maskEnabled: true
        // MultiEffect 默认把所有非零 alpha 阈值化，抹掉 QPainter 的边缘覆盖率。
        // 将下限过渡扩展至整个 alpha 区间，保留圆边的抗锯齿过渡。
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
        autoPaddingEnabled: false
        maskSource: ShaderEffectSource{
            textureSize: control.layer.textureSize
            smooth: true
            sourceItem: FluRectangle{
                radius: control.radius
                antialiasing: true
                smooth: true
                textureSize: Qt.size(Math.max(1, Math.ceil(width * 2)),
                                     Math.max(1, Math.ceil(height * 2)))
                width: control.width
                height: control.height
            }
        }
    }
}
