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

    builtinEffectModel: BreathRemovalViewModelFactory.createModel(root, root.instanceId)
    numNavigationPanels: 1
    property alias model: root.builtinEffectModel
    property NavigationPanel navPanel: NavigationPanel {
        name: "BreathRemovalControls"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Vertical
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
            text: qsTrc("effects/breathremoval", "Finds breaths between phrases (quiet, noisy, unvoiced) and turns them down with short fades.")
        }

        SliderWithTextInput {
            id: reductionSlider
            width: parent.width
            enabled: !model.silence
            navigationPanel: root.navPanel
            navigationOrderStart: 0
            text: qsTrc("effects/breathremoval", "Reduction (dB)")
            value: model.reduction
            from: -40
            to: 0
            step: 1
            decimals: 0
            onNewValueRequested: function (v) { model.reduction = v }
        }

        CheckBox {
            id: silenceBox
            navigation.panel: root.navPanel
            navigation.order: 2
            text: qsTrc("effects/breathremoval", "Silence breaths completely")
            checked: model.silence
            onClicked: model.silence = !checked
        }

        SliderWithTextInput {
            id: sensitivitySlider
            width: parent.width
            navigationPanel: root.navPanel
            navigationOrderStart: 3
            text: qsTrc("effects/breathremoval", "Sensitivity (catch quieter breaths)")
            value: model.sensitivity
            from: 0
            to: 100
            step: 1
            decimals: 0
            onNewValueRequested: function (v) { model.sensitivity = v }
        }

        SliderWithTextInput {
            width: parent.width
            navigationPanel: root.navPanel
            navigationOrderStart: 5
            text: qsTrc("effects/breathremoval", "Fade (ms)")
            value: model.fadeMs
            from: 1
            to: 50
            step: 1
            decimals: 0
            onNewValueRequested: function (v) { model.fadeMs = v }
        }
    }
}
