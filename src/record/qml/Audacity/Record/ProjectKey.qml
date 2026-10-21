/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui 1.0
import Muse.UiComponents

import "internal"

//! Playback toolbar: project key ("D min, A442" / "No key"); click to edit
FlatButton {
    id: root

    property var itemData: null

    height: 28
    minWidth: 96

    text: Boolean(itemData) ? qsTrc("record", "Key: %1").arg(itemData.label) : ""
    toolTipTitle: qsTrc("record", "Project key")
    toolTipDescription: qsTrc("record", "Key and tuning used by auto-comp to score intonation. Click to edit, detect or clear.")

    enabled: Boolean(itemData) ? itemData.enabled : false

    navigation.name: "ProjectKey"
    navigation.accessible.name: Boolean(itemData) ? itemData.accessibleLabel : toolTipTitle
    navigation.accessible.description: toolTipDescription

    onClicked: {
        if (popup.isOpened) {
            popup.close()
        } else {
            popup.open()
        }
    }

    ProjectKeyPopup {
        id: popup

        itemData: root.itemData
    }
}
