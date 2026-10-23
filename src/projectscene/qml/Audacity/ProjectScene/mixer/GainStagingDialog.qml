/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Automation

//! Gain staging: measures each track (peak, true peak, RMS, integrated loudness) and suggests
//! input trims reaching a target level; "Apply" sets them all as one undo step.
StyledDialogView {
    id: root

    title: qsTrc("automation", "Gain staging")

    contentWidth: 860
    contentHeight: 420
    resizable: true

    GainStagingModel {
        id: gsModel
    }

    Component.onCompleted: gsModel.analyze()

    readonly property var columnWidths: [28, 170, 78, 78, 78, 84, 78, 90, 96]

    function dbText(db) {
        return db <= -149 ? "-∞" : db.toFixed(1)
    }
    function signedText(db) {
        return (db > 0.05 ? "+" : "") + db.toFixed(1)
    }
    function unitText() {
        return gsModel.targetKind === 1 ? "LUFS" : (gsModel.targetKind === 2 ? "dBTP" : "dBFS RMS")
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundPrimaryColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            spacing: 10
            StyledTextLabel {
                text: qsTrc("automation", "Target")
            }
            StyledDropdown {
                id: kindBox
                Layout.preferredWidth: 150
                model: [
                    { text: qsTrc("automation", "RMS (dBFS)"), value: 0 },
                    { text: qsTrc("automation", "Loudness (LUFS)"), value: 1 },
                    { text: qsTrc("automation", "True peak (dBTP)"), value: 2 }
                ]
                currentIndex: gsModel.targetKind
                onActivated: function (index, value) {
                    gsModel.targetKind = value
                    gsTarget.value = gsModel.target
                }
            }
            StyledSlider {
                id: gsTarget
                Layout.preferredWidth: 220
                from: -60
                to: 0
                stepSize: 0.5
                value: gsModel.target
                onMoved: gsModel.target = value
            }
            StyledTextLabel {
                Layout.preferredWidth: 110
                horizontalAlignment: Text.AlignLeft
                text: gsModel.target.toFixed(1) + " " + root.unitText()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("automation", "Analyze")
                onClicked: gsModel.analyze()
            }
        }

        // header
        Row {
            Layout.fillWidth: true
            Repeater {
                model: ["", qsTrc("automation", "Track"), qsTrc("automation", "Peak dBFS"), qsTrc("automation", "True peak dBTP"),
                    qsTrc("automation", "RMS dBFS"), qsTrc("automation", "Integrated LUFS"), qsTrc("automation", "Trim now"),
                    qsTrc("automation", "Suggested trim"), qsTrc("automation", "True peak after")]
                delegate: StyledTextLabel {
                    required property string modelData
                    required property int index
                    width: root.columnWidths[index]
                    horizontalAlignment: index === 1 ? Text.AlignLeft : Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font: ui.theme.bodyBoldFont
                    text: modelData
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ui.theme.strokeColor
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: gsModel.rows
            spacing: 2
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 28
                color: index % 2 ? "transparent" : ui.theme.backgroundSecondaryColor

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    Item {
                        width: root.columnWidths[0]
                        height: 24
                        CheckBox {
                            anchors.centerIn: parent
                            enabled: modelData.hasSuggestion
                            checked: modelData.use
                            onClicked: gsModel.setUse(index, !checked)
                        }
                    }
                    StyledTextLabel {
                        width: root.columnWidths[1]
                        horizontalAlignment: Text.AlignLeft
                        text: modelData.name + (modelData.channels > 1 ? "  (stereo)" : "")
                    }
                    StyledTextLabel { width: root.columnWidths[2]; text: root.dbText(modelData.peakDb) }
                    StyledTextLabel {
                        width: root.columnWidths[3]
                        text: root.dbText(modelData.truePeakDb)
                        color: modelData.truePeakDb > 0 ? "#e05050" : ui.theme.fontPrimaryColor
                    }
                    StyledTextLabel { width: root.columnWidths[4]; text: root.dbText(modelData.rmsDb) }
                    StyledTextLabel { width: root.columnWidths[5]; text: root.dbText(modelData.lufs) }
                    StyledTextLabel { width: root.columnWidths[6]; text: root.signedText(modelData.trimDb) }
                    StyledTextLabel {
                        width: root.columnWidths[7]
                        font: ui.theme.bodyBoldFont
                        text: modelData.hasSuggestion ? root.signedText(modelData.suggestedDb) + " dB" : qsTrc("automation", "silent")
                    }
                    StyledTextLabel {
                        width: root.columnWidths[8]
                        text: modelData.hasSuggestion ? root.dbText(modelData.truePeakAfterDb) + (modelData.overAfter ? "  ⚠ over" : "") : "–"
                        color: modelData.overAfter ? "#e05050" : ui.theme.fontPrimaryColor
                    }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            opacity: 0.75
            text: qsTrc("automation", "Measured on each track's audio before trim, volume and effects. True peak: 4× oversampled (BS.1770-4). Loudness: EBU R128 integrated. A trim that would push the true peak over 0 dBTP is flagged.")
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("automation", "Apply %1 trims").arg(gsModel.applicableCount)
                accentButton: true
                enabled: gsModel.applicableCount > 0
                onClicked: gsModel.apply()
            }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.hide()
            }
        }
    }

}
