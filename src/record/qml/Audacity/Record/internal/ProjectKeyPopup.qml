/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui 1.0
import Muse.UiComponents

StyledPopupView {
    id: root

    property var itemData: null

    // Values being edited (applied with "Apply")
    property int editTonic: 0
    property bool editMinor: false
    property real editHz: 440.0
    property string summary: ""

    margins: 12
    contentWidth: 440
    contentHeight: content.implicitHeight

    function reset() {
        if (!Boolean(itemData)) {
            return
        }
        editTonic = itemData.tonic
        editMinor = itemData.minor
        editHz = itemData.tuningHz
        summary = itemData.hasKey ? "" : qsTrc("record", "No key is set: auto-comp detects the key from the takes.")
    }

    function centsText(hz) {
        var c = 1200 * Math.log(hz / 440) / Math.LN2
        return (c >= 0 ? "+" : "") + c.toFixed(1) + " " + qsTrc("record", "cents")
    }

    onOpened: {
        reset()
        rootDropdown.navigation.requestActive()
    }

    ColumnLayout {
        id: content

        width: root.contentWidth
        spacing: 10

        NavigationPanel {
            id: navPanel
            name: "ProjectKeyPopup"
            enabled: root.isOpened
            direction: NavigationPanel.Both
            section: root.navigationSection
            order: 1
            accessible.name: titleLabel.text
        }

        StyledTextLabel {
            id: titleLabel
            Layout.fillWidth: true
            text: qsTrc("record", "Project key")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                spacing: 4
                StyledTextLabel { text: qsTrc("record", "Root") }
                StyledDropdown {
                    id: rootDropdown
                    Layout.preferredWidth: 90
                    model: Boolean(root.itemData) ? root.itemData.tonicNames : []
                    currentIndex: root.editTonic
                    navigation.panel: navPanel
                    navigation.row: 0
                    navigation.column: 0
                    navigation.accessible.name: qsTrc("record", "Key root") + ": " + currentText
                    onActivated: function (index, value) { root.editTonic = index }
                }
            }

            ColumnLayout {
                spacing: 4
                StyledTextLabel { text: qsTrc("record", "Mode") }
                StyledDropdown {
                    id: modeDropdown
                    Layout.preferredWidth: 100
                    model: [ qsTrc("record", "Major"), qsTrc("record", "Minor") ]
                    currentIndex: root.editMinor ? 1 : 0
                    navigation.panel: navPanel
                    navigation.row: 0
                    navigation.column: 1
                    navigation.accessible.name: qsTrc("record", "Mode") + ": " + currentText
                    onActivated: function (index, value) { root.editMinor = index === 1 }
                }
            }
        }

        ColumnLayout {
            spacing: 4
            StyledTextLabel { text: qsTrc("record", "Tuning (A4)") }
            RowLayout {
                spacing: 8
                IncrementalPropertyControl {
                    id: hzControl
                    implicitWidth: 100
                    currentValue: root.editHz
                    step: 0.1
                    decimals: 1
                    minValue: 415
                    maxValue: 466
                    measureUnitsSymbol: " " + qsTrc("record", "Hz")
                    navigation.panel: navPanel
                    navigation.row: 1
                    navigation.column: 0
                    navigation.accessible.name: qsTrc("record", "Tuning reference, A equals") + " " + currentValue + " " + qsTrc("record", "hertz")
                    onValueEdited: function (newValue) { root.editHz = newValue }
                }
                StyledTextLabel {
                    text: root.centsText(root.editHz)
                    opacity: 0.75
                }
            }
        }

        StyledTextLabel {
            id: summaryLabel
            Layout.fillWidth: true
            Layout.maximumWidth: root.contentWidth
            visible: text !== ""
            text: root.summary
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("record", "Detect from selection")
                toolTipTitle: text
                toolTipDescription: qsTrc("record", "Estimate key and tuning from the selected audio; nothing is stored until you apply")
                navigation.panel: navPanel
                navigation.row: 2
                navigation.column: 0
                navigation.accessible.description: toolTipDescription
                onClicked: {
                    var r = root.itemData.detectFromSelection()
                    root.summary = r.summary
                    if (r.valid) {
                        root.editTonic = r.tonic
                        root.editMinor = r.minor
                        root.editHz = r.tuningHz
                    }
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTrc("record", "Clear key")
                enabled: Boolean(root.itemData) && root.itemData.hasKey
                navigation.panel: navPanel
                navigation.row: 2
                navigation.column: 1
                navigation.accessible.description: qsTrc("record", "Remove the project key; auto-comp then detects the key from the takes")
                onClicked: {
                    root.itemData.clear()
                    root.close()
                }
            }

            FlatButton {
                text: qsTrc("record", "Apply")
                accentButton: true
                navigation.panel: navPanel
                navigation.row: 2
                navigation.column: 2
                navigation.accessible.description: qsTrc("record", "Store this key and tuning as the project key (undoable)")
                onClicked: {
                    root.itemData.apply(root.editTonic, root.editMinor, root.editHz)
                    root.close()
                }
            }
        }
    }
}
