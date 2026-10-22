/*
* Audacity: A Digital Audio Editor
*/
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Record

//! Live monitoring controls: monitor input, through effects, low-latency, level, mute and the
//! latency / effects-heard readout. Recordings always stay dry.
ColumnLayout {
    id: root

    property NavigationPanel navigationPanel: null
    property int navigationOrderStart: 0
    property bool compact: false

    spacing: 6

    LiveMonitoringModel {
        id: monitor
    }
    Component.onCompleted: monitor.init()

    CheckBox {
        text: qsTrc("record", "Monitor input (hear yourself through the track's effects)")
        checked: monitor.monitorInput
        navigation.panel: root.navigationPanel
        navigation.order: root.navigationOrderStart
        onClicked: monitor.monitorInput = !checked
    }
    CheckBox {
        text: qsTrc("record", "Monitor through realtime effects (recording stays dry)")
        checked: monitor.throughEffects
        enabled: monitor.monitorInput
        navigation.panel: root.navigationPanel
        navigation.order: root.navigationOrderStart + 1
        onClicked: monitor.throughEffects = !checked
    }
    CheckBox {
        text: qsTrc("record", "Low-latency monitoring (bypass effects over %1 ms)").arg(monitor.thresholdMs)
        checked: monitor.lowLatency
        enabled: monitor.monitorInput && monitor.throughEffects
        navigation.panel: root.navigationPanel
        navigation.order: root.navigationOrderStart + 2
        onClicked: monitor.lowLatency = !checked
    }
    RowLayout {
        spacing: 8
        StyledTextLabel { text: qsTrc("record", "Monitor level") }
        StyledSlider {
            Layout.preferredWidth: 180
            from: -60
            to: 12
            stepSize: 1
            value: monitor.gainDb
            enabled: monitor.monitorInput
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderStart + 3
            navigation.accessible.name: qsTrc("record", "Monitor level") + " " + Math.round(value) + " dB"
            onMoved: monitor.gainDb = value
        }
        StyledTextLabel { text: (monitor.gainDb > 0 ? "+" : "") + Math.round(monitor.gainDb) + " dB" }
        FlatButton {
            text: monitor.muted ? qsTrc("record", "Unmute") : qsTrc("record", "Mute")
            accentButton: monitor.muted
            enabled: monitor.monitorInput
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrderStart + 4
            onClicked: monitor.toggleMute()
        }
    }
    StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        text: monitor.latencyText
        visible: monitor.monitorInput
    }
    StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        text: monitor.effectsText
        visible: monitor.monitorInput
    }
    StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        color: "#E8A33D"
        text: monitor.bypassedText
        visible: monitor.monitorInput && monitor.hasBypassed
    }
}
