/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Record

//! Artist Recording Mode: a focused, large-type recording dashboard drawn over the project
//! window. Hiding it (Escape, the Exit button, View > Artist recording mode, Ctrl+Shift+R)
//! leaves the normal layout untouched underneath.
FocusScope {
    id: root

    property NavigationSection navigationSection: NavigationSection {
        name: "ArtistRecordingMode"
        order: 0
        enabled: root.visible
    }

    //! false where the dashboard does not apply (e.g. the Home page)
    property bool allowed: true

    visible: allowed && model.active
    focus: visible

    ArtistModeModel {
        id: model
    }

    Component.onCompleted: model.init()
    onVisibleChanged: if (visible) recordButton.navigation.requestActive()

    // Escape leaves the mode wherever keyboard focus is (Space, R etc. keep their global shortcuts)
    Shortcut {
        sequence: "Escape"
        enabled: root.visible
        context: Qt.WindowShortcut
        onActivated: model.exitMode()
    }

    // swallow mouse input so the project underneath is not edited by accident
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(w) { w.accepted = true }
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundSecondaryColor
    }

    NavigationPanel {
        id: transportPanel
        name: "ArtistTransport"
        section: root.navigationSection
        order: 0
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("record", "Transport")
    }
    NavigationPanel {
        id: optionsPanel
        name: "ArtistOptions"
        section: root.navigationSection
        order: 1
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("record", "Recording options")
    }
    NavigationPanel {
        id: takesPanel
        name: "ArtistTakes"
        section: root.navigationSection
        order: 2
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("record", "Takes")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 20

        // header: what is being recorded
        RowLayout {
            Layout.fillWidth: true
            StyledTextLabel {
                text: qsTrc("record", "Artist recording mode")
                font: ui.theme.headerBoldFont
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("record", "Exit")
                toolTipShortcut: "Esc"
                navigation.panel: takesPanel
                navigation.order: 10
                onClicked: model.exitMode()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 32

            // left: timer, track/take, transport
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                StyledTextLabel {
                    id: timer
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: model.positionText
                    font.family: ui.theme.bodyFont.family
                    font.pixelSize: 96
                    font.bold: true
                    color: model.isRecording ? "#E0433D" : ui.theme.fontPrimaryColor
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 28
                    text: (model.isRecording ? qsTrc("record", "Recording take %1").arg(model.takeNumber)
                                             : qsTrc("record", "Next take: %1").arg(model.takeNumber))
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 20
                    text: model.trackName.length > 0
                          ? qsTrc("record", "Track: %1   ·   %2 take lane(s)").arg(model.trackName).arg(model.takeLaneCount)
                          : qsTrc("record", "No track selected: recording goes to a new track")
                }

                RowLayout {
                    spacing: 16
                    Layout.topMargin: 12

                    FlatButton {
                        id: recordButton
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 120
                        icon: model.isRecording ? IconCode.STOP_FILL : IconCode.RECORD_FILL
                        iconFont: ui.theme.toolbarIconsFont
                        iconColor: "#E0433D"
                        text: model.isRecording ? qsTrc("record", "Stop recording")
                                                : (model.countIn ? qsTrc("record", "Record (count-in)") : qsTrc("record", "Record"))
                        textFont.pixelSize: 18
                        navigation.panel: transportPanel
                        navigation.order: 0
                        onClicked: model.toggleRecord()
                    }
                    FlatButton {
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 120
                        icon: IconCode.STOP_FILL
                        iconFont: ui.theme.toolbarIconsFont
                        text: qsTrc("record", "Stop")
                        textFont.pixelSize: 18
                        navigation.panel: transportPanel
                        navigation.order: 1
                        onClicked: model.stop()
                    }
                    FlatButton {
                        Layout.preferredWidth: 160
                        Layout.preferredHeight: 120
                        icon: model.isPlaying ? IconCode.PAUSE_FILL : IconCode.PLAY_FILL
                        iconFont: ui.theme.toolbarIconsFont
                        iconColor: "#2FAE61"
                        text: model.isPlaying ? qsTrc("record", "Stop playback") : qsTrc("record", "Play")
                        textFont.pixelSize: 18
                        toolTipShortcut: "Space"
                        navigation.panel: transportPanel
                        navigation.order: 2
                        onClicked: model.togglePlay()
                    }
                }

                RowLayout {
                    spacing: 12
                    FlatButton {
                        text: qsTrc("record", "Next take")
                        navigation.panel: takesPanel
                        navigation.order: 0
                        onClicked: model.nextTake()
                    }
                    FlatButton {
                        text: qsTrc("record", "Activate take")
                        navigation.panel: takesPanel
                        navigation.order: 1
                        onClicked: model.activateTake()
                    }
                    FlatButton {
                        text: qsTrc("record", "Auto-comp")
                        navigation.panel: takesPanel
                        navigation.order: 2
                        onClicked: model.autoComp()
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: 4
                    color: model.lastTakeClipped ? "#55E0433D" : ui.theme.backgroundPrimaryColor
                    StyledTextLabel {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 18
                        text: model.lastTakeText
                    }
                }
            }

            // right: project info, meter, options
            ColumnLayout {
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                spacing: 16

                GridLayout {
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 4
                    Repeater {
                        model: [
                            [qsTrc("record", "Tempo"), model.tempoText],
                            [qsTrc("record", "Time signature"), model.timeSignatureText],
                            [qsTrc("record", "Key"), model.keyText]
                        ]
                        delegate: StyledTextLabel {
                            required property var modelData
                            Layout.columnSpan: 2
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 24
                            text: modelData[0] + ":  " + modelData[1]
                        }
                    }
                }

                // big input meter: level bar, peak-hold line, latched clip lamp (click to reset)
                StyledTextLabel {
                    text: qsTrc("record", "Input")
                    font: ui.theme.bodyBoldFont
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    readonly property real minDb: -60
                    function pos(db) { return Math.max(0, Math.min(1, (db - minDb) / -minDb)) }

                    Rectangle {
                        id: meterBg
                        anchors.left: parent.left
                        anchors.right: clipLamp.left
                        anchors.rightMargin: 8
                        height: parent.height
                        color: ui.theme.backgroundPrimaryColor
                        border.color: ui.theme.strokeColor
                        Rectangle {
                            height: parent.height - 4
                            y: 2; x: 2
                            width: (parent.width - 4) * parent.parent.pos(model.inputLevelDb)
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#2FAE61" }
                                GradientStop { position: 0.8; color: "#E8C547" }
                                GradientStop { position: 1.0; color: "#E0433D" }
                            }
                        }
                        Rectangle {
                            width: 3
                            height: parent.height
                            x: 2 + (parent.width - 7) * parent.parent.pos(model.peakHoldDb)
                            color: ui.theme.fontPrimaryColor
                            visible: model.peakHoldDb > parent.parent.minDb
                        }
                    }
                    Rectangle {
                        id: clipLamp
                        anchors.right: parent.right
                        width: 56; height: parent.height
                        radius: 4
                        color: model.inputClipped ? "#E0433D" : ui.theme.backgroundPrimaryColor
                        border.color: ui.theme.strokeColor
                        StyledTextLabel { anchors.centerIn: parent; text: qsTrc("record", "CLIP") }
                        MouseArea { anchors.fill: parent; onClicked: model.resetClipIndicator() }
                    }
                }
                StyledTextLabel {
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("record", "Peak %1 dBFS").arg(model.peakHoldDb <= -144 ? "-inf" : model.peakHoldDb.toFixed(1))
                }

                StyledTextLabel {
                    Layout.topMargin: 12
                    text: qsTrc("record", "Options")
                    font: ui.theme.bodyBoldFont
                }
                CheckBox {
                    text: qsTrc("record", "Metronome (not available yet)")
                    checked: model.metronome
                    enabled: false
                    navigation.panel: optionsPanel
                    navigation.order: 0
                    onClicked: model.metronome = !checked
                }
                CheckBox {
                    text: qsTrc("record", "Count-in (%1 s lead-in)").arg(model.countInSeconds)
                    checked: model.countIn
                    navigation.panel: optionsPanel
                    navigation.order: 1
                    onClicked: model.countIn = !checked
                }
                CheckBox {
                    text: qsTrc("record", "Punch out at selection end")
                    checked: model.punchOut
                    navigation.panel: optionsPanel
                    navigation.order: 2
                    onClicked: model.punchOut = !checked
                }
                CheckBox {
                    text: qsTrc("record", "Keep takes when recording over")
                    checked: model.keepTakes
                    navigation.panel: optionsPanel
                    navigation.order: 3
                    onClicked: model.keepTakes = !checked
                }
                StyledTextLabel {
                    Layout.topMargin: 12
                    text: qsTrc("record", "Monitoring")
                    font: ui.theme.bodyBoldFont
                }
                MonitoringOptions {
                    Layout.fillWidth: true
                    navigationPanel: optionsPanel
                    navigationOrderStart: 10
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
