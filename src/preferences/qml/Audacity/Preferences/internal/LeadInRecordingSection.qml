/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.UiComponents 1.0

BaseSection {
    id: root

    title: qsTrc("preferences", "Recording lead-in time")
    spacing: 16

    property var recordingPreferencesModel: null

    navigation.direction: NavigationPanel.Both

    IncrementalPropertyControlWithTitle {
        title: qsTrc("preferences", "Lead-in time before recording starts")

        currentValue: recordingPreferencesModel.leadInTimeDuration

        columnWidth: root.columnWidth
        controlWidth: 120
        spacing: root.columnSpacing

        minValue: 0
        maxValue: 30
        step: 1
        decimals: 1

        measureUnitsSymbol: qsTrc("global", "seconds")

        navigation.name: "LeadInTimeDurationControl"
        navigation.panel: root.navigation
        navigation.row: 0

        onValueEdited: function (newValue) {
            recordingPreferencesModel.setLeadInTimeDuration(newValue)
        }
    }

    IncrementalPropertyControlWithTitle {
        title: qsTrc("preferences", "Crossfade")

        currentValue: recordingPreferencesModel.crossfadeDuration

        columnWidth: root.columnWidth
        controlWidth: 120
        spacing: root.columnSpacing

        minValue: 0
        maxValue: 100
        step: 1
        decimals: 1

        measureUnitsSymbol: qsTrc("global", "ms")

        navigation.name: "CrossfadeDurationControl"
        navigation.panel: root.navigation
        navigation.row: 1

        onValueEdited: function (newValue) {
            recordingPreferencesModel.setCrossfadeDuration(newValue)
        }
    }

    CheckBox {
        width: parent.width
        text: qsTrc("preferences", "Stop lead-in recording at the end of the selection (punch-out)")
        checked: recordingPreferencesModel.punchOutAtSelectionEnd

        navigation.name: "PunchOutAtSelectionEndCheckBox"
        navigation.panel: root.navigation
        navigation.row: 2

        onClicked: {
            recordingPreferencesModel.punchOutAtSelectionEnd = !checked
        }
    }

    CheckBox {
        width: parent.width
        text: qsTrc("preferences", "Keep overwritten audio as take lanes when recording over a region")
        checked: recordingPreferencesModel.keepTakesWhenRecordingOver

        navigation.name: "KeepTakesWhenRecordingOverCheckBox"
        navigation.panel: root.navigation
        navigation.row: 3

        onClicked: {
            recordingPreferencesModel.keepTakesWhenRecordingOver = !checked
        }
    }

    IncrementalPropertyControlWithTitle {
        title: qsTrc("preferences", "Auto-comp: shortest segment")

        currentValue: recordingPreferencesModel.autoCompSegmentLength

        columnWidth: root.columnWidth
        controlWidth: 120
        spacing: root.columnSpacing

        minValue: 0.5
        maxValue: 30
        step: 0.5
        decimals: 1

        measureUnitsSymbol: qsTrc("global", "seconds")

        navigation.name: "AutoCompSegmentLengthControl"
        navigation.panel: root.navigation
        navigation.row: 4

        onValueEdited: function (newValue) {
            recordingPreferencesModel.setAutoCompSegmentLength(newValue)
        }
    }
}
