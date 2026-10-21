import QtQuick
import Muse.UiComponents
import Audacity.Effects
import Audacity.BuiltinEffects
import Audacity.BuiltinEffectsCollection

BuiltinEffectBase {
    id: root

    property string title: model.effectTitle
    property bool isApplyAllowed: true

    width: 360
    implicitHeight: column.height

    builtinEffectModel: NoiseSuppressionViewModelFactory.createModel(root, root.instanceId)
    numNavigationPanels: 1
    property alias model: root.builtinEffectModel
    property NavigationPanel navPanel: NavigationPanel {
        name: "NoiseSuppressionControls"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        section: root.dialogView ? root.dialogView.navigationSection : null
        order: 1
    }

    Column {
        id: column
        width: parent.width
        spacing: 16

        StyledTextLabel {
            width: parent.width
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("effects/noisesuppression", "Removes background noise from speech and vocals using RNNoise. No noise profile needed.")
        }

        SliderWithTextInput {
            width: parent.width
            navigationPanel: root.navPanel
            navigationOrderStart: 0
            text: qsTrc("effects/noisesuppression", "Strength (%)")
            value: model.mix
            from: 0
            to: 100
            step: 1
            decimals: 0
            onNewValueRequested: function (v) { model.mix = v }
        }
    }
}
