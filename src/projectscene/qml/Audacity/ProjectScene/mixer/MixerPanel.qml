/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Automation

//! Channel-strip mixer: one strip per track and bus, plus master.
//! Each strip: realtime effect inserts, sends, output routing, pan, fader with meter, mute/solo.
StyledDialogView {
    id: root

    title: qsTrc("automation", "Mixer")

    contentWidth: 1100
    contentHeight: 640

    resizable: true

    readonly property int stripWidth: 156

    MixerPanelModel {
        id: mixer
    }

    Component.onCompleted: mixer.init()

    function dbText(db) {
        return db <= -59.9 ? "-∞" : (db > 0 ? "+" : "") + db.toFixed(1)
    }

    function levelText(level) {
        return level <= 0.0001 ? "-∞ dB" : (20 * Math.log(level) / Math.LN10).toFixed(1) + " dB"
    }

    function panText(pan) {
        var p = Math.round(pan * 100)
        return p === 0 ? "C" : (p < 0 ? "L" + (-p) : "R" + p)
    }

    function meterFraction(db) {
        return Math.max(0, Math.min(1, (db + 60) / 60))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            spacing: 8
            FlatButton {
                text: qsTrc("automation", "Add bus")
                onClicked: mixer.addBus()
            }
            StyledTextLabel {
                text: qsTrc("automation", "%1 strips").arg(mixer.strips.length)
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: stripRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick

            Row {
                id: stripRow
                height: parent.height
                spacing: 6

                Repeater {
                    model: mixer.strips

                    delegate: Rectangle {
                        id: strip

                        required property var modelData
                        required property int index

                        readonly property bool isMaster: modelData.kind === "master"
                        readonly property bool isBus: modelData.kind === "bus"
                        readonly property var meter: mixer.meters.length > index ? mixer.meters[index] : [-60, -60]

                        width: root.stripWidth
                        height: stripRow.height
                        radius: 4
                        color: ui.theme.backgroundSecondaryColor
                        border.color: isMaster ? "#3f6b4a" : (isBus ? "#8a6a3a" : ui.theme.strokeColor)
                        border.width: isMaster || isBus ? 2 : 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 4

                            // name
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                radius: 3
                                color: strip.isMaster ? "#2f4f38" : (strip.isBus ? "#4a3a28" : ui.theme.buttonColor)
                                StyledTextLabel {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    text: strip.modelData.name
                                    color: strip.isMaster || strip.isBus ? "white" : ui.theme.fontPrimaryColor
                                    font: ui.theme.bodyBoldFont
                                }
                            }

                            // inserts
                            RowLayout {
                                Layout.fillWidth: true
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    text: qsTrc("automation", "Inserts")
                                }
                                CheckBox {
                                    checked: strip.modelData.insertsActive
                                    onClicked: mixer.setInsertsActive(strip.modelData.trackId, !checked)
                                }
                            }
                            Column {
                                Layout.fillWidth: true
                                spacing: 2
                                Repeater {
                                    model: strip.modelData.inserts
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: parent.width
                                        height: 22
                                        radius: 2
                                        color: modelData.active ? ui.theme.buttonColor : ui.theme.backgroundPrimaryColor
                                        opacity: strip.modelData.insertsActive ? 1 : 0.5
                                        CheckBox {
                                            id: fxOn
                                            anchors.left: parent.left
                                            anchors.leftMargin: 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            checked: modelData.active
                                            onClicked: mixer.setInsertActive(strip.modelData.trackId, modelData.index, !checked)
                                        }
                                        StyledTextLabel {
                                            anchors.left: fxOn.right
                                            anchors.leftMargin: 4
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignLeft
                                            text: modelData.name
                                        }
                                    }
                                }
                                StyledTextLabel {
                                    visible: strip.modelData.inserts.length === 0
                                    text: qsTrc("automation", "No effects")
                                    opacity: 0.6
                                }
                            }

                            // sends
                            StyledTextLabel {
                                visible: !strip.isMaster
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: qsTrc("automation", "Sends")
                            }
                            Column {
                                visible: !strip.isMaster
                                Layout.fillWidth: true
                                spacing: 1
                                Repeater {
                                    model: strip.isMaster ? [] : strip.modelData.sends
                                    delegate: Column {
                                        required property var modelData
                                        width: parent.width
                                        RowLayout {
                                            width: parent.width
                                            StyledTextLabel {
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignLeft
                                                text: modelData.name + " " + root.levelText(modelData.level)
                                            }
                                            FlatButton {
                                                Layout.preferredWidth: 20
                                                Layout.preferredHeight: 20
                                                icon: IconCode.CLOSE_X_ROUNDED
                                                transparent: true
                                                onClicked: mixer.removeSend(strip.modelData.trackId, modelData.busId)
                                            }
                                        }
                                        StyledSlider {
                                            width: parent.width
                                            from: 0
                                            to: 1
                                            value: modelData.level
                                            onMoved: mixer.setSend(strip.modelData.trackId, modelData.busId, value, false)
                                            onPressedChanged: {
                                                if (!pressed) {
                                                    mixer.setSend(strip.modelData.trackId, modelData.busId, value, true)
                                                }
                                            }
                                        }
                                    }
                                }
                                StyledDropdown {
                                    width: parent.width
                                    visible: !strip.isMaster && strip.modelData.sendTargets.length > 0
                                    displayText: qsTrc("automation", "Add send…")
                                    currentIndex: -1
                                    model: {
                                        var opts = []
                                        var t = strip.isMaster ? [] : strip.modelData.sendTargets
                                        for (var i = 0; i < t.length; ++i) {
                                            opts.push({ text: t[i].name, value: t[i].busId })
                                        }
                                        return opts
                                    }
                                    onActivated: function (index, value) {
                                        mixer.setSend(strip.modelData.trackId, value, 0.5, true)
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }

                            // output
                            StyledDropdown {
                                visible: !strip.isMaster
                                Layout.fillWidth: true
                                model: {
                                    var opts = []
                                    var o = strip.isMaster ? [] : strip.modelData.outputs
                                    for (var i = 0; i < o.length; ++i) {
                                        opts.push({ text: "→ " + o[i].name, value: o[i].busId })
                                    }
                                    return opts
                                }
                                currentIndex: {
                                    for (var i = 0; i < model.length; ++i) {
                                        if (model[i].value === strip.modelData.output) {
                                            return i
                                        }
                                    }
                                    return 0
                                }
                                onActivated: function (index, value) {
                                    mixer.setOutput(strip.modelData.trackId, value)
                                }
                            }
                            StyledTextLabel {
                                visible: strip.isMaster
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                text: qsTrc("automation", "→ Output device")
                            }

                            // pan
                            RowLayout {
                                Layout.fillWidth: true
                                opacity: strip.isMaster ? 0.3 : 1
                                StyledTextLabel {
                                    text: qsTrc("automation", "Pan")
                                }
                                StyledSlider {
                                    id: panSlider
                                    Layout.fillWidth: true
                                    enabled: !strip.isMaster
                                    from: -1
                                    to: 1
                                    value: strip.modelData.pan
                                    onMoved: mixer.setPan(strip.modelData.trackId, value, false)
                                    onPressedChanged: {
                                        if (!pressed) {
                                            mixer.setPan(strip.modelData.trackId, value, true)
                                        }
                                    }
                                }
                                StyledTextLabel {
                                    Layout.preferredWidth: 34
                                    text: root.panText(panSlider.value)
                                }
                            }

                            // fader + meter
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 190
                                spacing: 4

                                StyledSlider {
                                    id: fader
                                    Layout.fillHeight: true
                                    Layout.preferredWidth: 30
                                    orientation: Qt.Vertical
                                    from: -60
                                    to: 12
                                    value: strip.modelData.volumeDb
                                    onMoved: mixer.setVolume(strip.modelData.trackId, value, false)
                                    onPressedChanged: {
                                        if (!pressed) {
                                            mixer.setVolume(strip.modelData.trackId, value, true)
                                        }
                                    }
                                }

                                Row {
                                    Layout.fillHeight: true
                                    spacing: 2
                                    Repeater {
                                        model: 2
                                        delegate: Rectangle {
                                            required property int index
                                            width: 8
                                            height: parent.height
                                            color: "#1e1e24"
                                            radius: 1
                                            Rectangle {
                                                readonly property real db: strip.meter[index]
                                                anchors.bottom: parent.bottom
                                                width: parent.width
                                                height: parent.height * root.meterFraction(db)
                                                color: db > -1 ? "#e05050" : (db > -12 ? "#e0c050" : "#50c070")
                                            }
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        text: root.dbText(fader.value) + " dB"
                                    }
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        opacity: 0.7
                                        text: qsTrc("automation", "peak") + "\n" + root.dbText(Math.max(strip.meter[0], strip.meter[1]))
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }

                            // mute / solo
                            RowLayout {
                                Layout.fillWidth: true
                                visible: !strip.isMaster
                                FlatButton {
                                    Layout.fillWidth: true
                                    text: qsTrc("automation", "M")
                                    accentButton: strip.modelData.mute
                                    onClicked: mixer.setMute(strip.modelData.trackId, !strip.modelData.mute)
                                }
                                FlatButton {
                                    Layout.fillWidth: true
                                    text: qsTrc("automation", "S")
                                    accentButton: strip.modelData.solo
                                    onClicked: mixer.setSolo(strip.modelData.trackId, !strip.modelData.solo)
                                }
                            }
                            Item {
                                visible: strip.isMaster
                                Layout.preferredHeight: 28
                            }
                        }
                    }
                }
            }
        }
    }
}
