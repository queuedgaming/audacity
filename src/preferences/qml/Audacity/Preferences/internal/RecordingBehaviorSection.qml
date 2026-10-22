/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.UiComponents 1.0

BaseSection {
    id: root

    title: qsTrc("preferences", "Recording behavior")

    property var recordingPreferencesModel: null

    Column {
        width: parent.width
        spacing: 12

        // input monitoring itself lives in the "Live monitoring" section (Monitor input)
        CheckBox {
            id: micMeteringCheckBox

            width: parent.width

            text: qsTrc("preferences", "Show mic metering when not recording")

            checked: recordingPreferencesModel.isMicMeteringOn

            navigation.name: "MicMeteringCheckBox"
            navigation.panel: root.navigation
            navigation.order: 1

            onClicked: {
                recordingPreferencesModel.isMicMeteringOn = !checked
            }
        }
    }
}
