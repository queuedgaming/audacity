import QtQuick 2.15
import QtQuick.Layouts

import Muse.UiComponents

import Audacity.Effects
import Audacity.BuiltinEffects
import Audacity.BuiltinEffectsCollection

BuiltinEffectBase {
    id: root

    property string title: qsTrc("effects/truepeak", "True Peak Limiter")
    property bool isApplyAllowed: true

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    builtinEffectModel: TruePeakLimiterViewModelFactory.createModel(root, root.instanceId)
    numNavigationPanels: 1
    property alias limiter: root.builtinEffectModel

    property NavigationPanel knobsNavigationPanel: NavigationPanel {
        name: "TruePeakLimiterKnobs"
        enabled: root.enabled && root.visible
        direction: NavigationPanel.Horizontal
        section: root.dialogView ? root.dialogView.navigationSection : null
        order: 1
    }

    ColumnLayout {
        id: column

        width: parent.width
        spacing: 16

        RoundedRectangle {
            Layout.preferredWidth: knobs.implicitWidth + 2 * 32
            Layout.preferredHeight: knobs.implicitHeight + 2 * 24

            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.strokeColor
            border.width: 1
            radius: 4

            RowLayout {
                id: knobs

                anchors.centerIn: parent
                spacing: 48

                Repeater {
                    model: ["ceilingDb", "releaseMs", "lookaheadMs"]

                    BigParameterKnob {
                        required property string modelData
                        required property int index

                        navigation.panel: root.knobsNavigationPanel
                        navigation.order: index

                        parameter: limiter.paramsList[modelData]
                        radius: 24

                        onNewValueRequested: function (key, newValue) {
                            limiter.setParam(key, newValue)
                        }
                        onCommitRequested: limiter.commitSettings()
                    }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.bottomMargin: root.usedDestructively ? 16 : 0
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("effects/truepeak", "8× oversampled true-peak detection (ITU-R BS.1770-4 style); output never exceeds the ceiling.")
                  + "\n" + limiter.latencyText
        }
    }
}
