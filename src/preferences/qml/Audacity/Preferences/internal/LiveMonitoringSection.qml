/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick 2.15

import Muse.Ui 1.0
import Muse.UiComponents

import Audacity.UiComponents 1.0
import Audacity.Record

BaseSection {
    id: root

    title: qsTrc("preferences", "Live monitoring")

    MonitoringOptions {
        width: parent.width
        navigationPanel: root.navigation
        navigationOrderStart: 1
    }
}
