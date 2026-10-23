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

        RoundedRectangle {
            id: meterBox

            Layout.fillWidth: true
            Layout.preferredHeight: meterCol.implicitHeight + 2 * 12

            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.strokeColor
            border.width: 1
            radius: 4

            readonly property real rangeDb: 24
            function fmt(v) { return v.toFixed(1) }

            ColumnLayout {
                id: meterCol

                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    StyledTextLabel {
                        text: qsTrc("effects/truepeak", "Gain reduction")
                        font: ui.theme.bodyBoldFont
                    }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel {
                        objectName: "grReadout"
                        text: qsTrc("effects/truepeak", "Now %1 dB   Max %2 dB")
                              .arg(limiter.meterActive ? "-" + meterBox.fmt(limiter.currentGrDb) : "—")
                              .arg("-" + meterBox.fmt(limiter.maxGrDb))
                    }
                    FlatButton {
                        text: qsTrc("effects/truepeak", "Reset")
                        onClicked: limiter.resetMeter()
                    }
                }

                // bar grows from the right edge (0 dB) to the left, 24 dB full scale
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor
                    radius: 2

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 1
                        width: (parent.width - 2) * Math.min(1, limiter.currentGrDb / meterBox.rangeDb)
                        color: "#E8A33D"
                        Behavior on width { NumberAnimation { duration: 30 } }
                    }
                    Rectangle {
                        // held maximum
                        visible: limiter.maxGrDb > 0
                        x: parent.width - 1 - (parent.width - 2) * Math.min(1, limiter.maxGrDb / meterBox.rangeDb)
                        width: 2
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: "#D84B3E"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel { text: "-24"; opacity: 0.6 }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel { text: "-12"; opacity: 0.6 }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel { text: "0 dB"; opacity: 0.6 }
                }

                StyledTextLabel {
                    objectName: "tpReadout"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("effects/truepeak", "Output true peak: %1 dBTP   (max %2 dBTP)")
                          .arg(limiter.meterActive && limiter.outputPeakDb > -119 ? meterBox.fmt(limiter.outputPeakDb) : "—")
                          .arg(limiter.maxOutputPeakDb > -119 ? meterBox.fmt(limiter.maxOutputPeakDb) : "—")
                          + (limiter.demoFeed ? "   " + qsTrc("effects/truepeak", "[demo feed]") : "")
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
