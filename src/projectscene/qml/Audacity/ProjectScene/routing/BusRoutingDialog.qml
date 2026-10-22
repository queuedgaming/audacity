/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import Audacity.Automation

//! Node graph of the project's signal routing: tracks -> buses -> master.
//! Drag from a node's output port onto a bus or Master to connect it (output or send,
//! depending on the drag mode); edit the selected node's output and sends on the right.
StyledDialogView {
    id: root

    title: qsTrc("automation", "Bus routing")

    contentWidth: 980
    contentHeight: 600

    resizable: true

    property var selectedTrackId: null
    property bool sendMode: false

    BusRoutingGraphModel {
        id: graph
        onChanged: {
            nodeRepeaterRefresh()
            edges.requestPaint()
        }
    }

    Component.onCompleted: graph.init()

    function nodeRepeaterRefresh() {
        // keep the selection if the node still exists
        if (selectedTrackId !== null && !findNode(selectedTrackId)) {
            selectedTrackId = null
        }
    }

    function allNodes() { return graph.tracks.concat(graph.buses) }

    function findNode(trackId) {
        var all = allNodes()
        for (var i = 0; i < all.length; ++i) {
            if (all[i].trackId === trackId) {
                return all[i]
            }
        }
        return null
    }

    function busName(busId) {
        if (busId === "") {
            return qsTrc("automation", "Master")
        }
        for (var i = 0; i < graph.buses.length; ++i) {
            if (graph.buses[i].busId === busId) {
                return graph.buses[i].name
            }
        }
        return busId
    }

    function levelText(level) {
        if (level <= 0.0001) {
            return "-∞ dB"
        }
        return (20 * Math.log(level) / Math.LN10).toFixed(1) + " dB"
    }

    // Node geometry (graph coordinates)
    readonly property int nodeW: 170
    readonly property int nodeH: 40
    readonly property int colTracks: 20
    readonly property int colBuses: Math.max(colTracks + nodeW + 60, (canvasArea.width - nodeW) / 2)
    readonly property int colMaster: Math.max(colBuses + nodeW + 60, canvasArea.width - nodeW - 24)

    function nodePos(trackId) {
        for (var i = 0; i < graph.tracks.length; ++i) {
            if (graph.tracks[i].trackId === trackId) {
                return Qt.point(colTracks, 20 + i * (nodeH + 16))
            }
        }
        for (var j = 0; j < graph.buses.length; ++j) {
            if (graph.buses[j].trackId === trackId) {
                return Qt.point(colBuses, 20 + j * (nodeH + 40))
            }
        }
        return Qt.point(0, 0)
    }

    function busPos(busId) {
        if (busId === "") {
            return masterPos()
        }
        for (var j = 0; j < graph.buses.length; ++j) {
            if (graph.buses[j].busId === busId) {
                return Qt.point(colBuses, 20 + j * (nodeH + 40))
            }
        }
        return masterPos()
    }

    function masterPos() {
        var n = Math.max(graph.tracks.length, 1)
        return Qt.point(colMaster, Math.max(20, (n * (nodeH + 16)) / 2 - nodeH / 2))
    }

    //! bus id under graph point p ("" = master, undefined = nothing)
    function dropTarget(p) {
        for (var j = 0; j < graph.buses.length; ++j) {
            var bp = busPos(graph.buses[j].busId)
            if (p.x >= bp.x && p.x <= bp.x + nodeW && p.y >= bp.y && p.y <= bp.y + nodeH) {
                return graph.buses[j].busId
            }
        }
        var mp = masterPos()
        if (p.x >= mp.x && p.x <= mp.x + nodeW && p.y >= mp.y && p.y <= mp.y + nodeH) {
            return ""
        }
        return undefined
    }

    property point dragFrom: Qt.point(0, 0)
    property point dragTo: Qt.point(0, 0)
    property bool dragging: false
    property var dragTrackId: null

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ---- graph ---------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                spacing: 8

                FlatButton {
                    text: qsTrc("automation", "Add bus")
                    onClicked: graph.addBus()
                }

                StyledTextLabel {
                    text: qsTrc("automation", "Drag from a port to connect:")
                }

                FlatRadioButton {
                    width: 90
                    height: 28
                    text: qsTrc("automation", "Output")
                    checked: !root.sendMode
                    onToggled: root.sendMode = false
                }

                FlatRadioButton {
                    width: 90
                    height: 28
                    text: qsTrc("automation", "Send")
                    checked: root.sendMode
                    onToggled: root.sendMode = true
                }
            }

            Rectangle {
                id: canvasArea

                Layout.fillWidth: true
                Layout.fillHeight: true

                color: ui.theme.backgroundSecondaryColor
                border.color: ui.theme.strokeColor
                radius: 4
                clip: true

                onWidthChanged: edges.requestPaint()

                Canvas {
                    id: edges

                    anchors.fill: parent

                    function curve(ctx, a, b) {
                        var mx = (a.x + b.x) / 2
                        ctx.beginPath()
                        ctx.moveTo(a.x, a.y)
                        ctx.bezierCurveTo(mx, a.y, mx, b.y, b.x, b.y)
                        ctx.stroke()
                    }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var all = root.allNodes()
                        for (var i = 0; i < all.length; ++i) {
                            var n = all[i]
                            var p = root.nodePos(n.trackId)
                            var from = Qt.point(p.x + root.nodeW, p.y + root.nodeH / 2)
                            var t = root.busPos(n.output)
                            var to = Qt.point(t.x, t.y + root.nodeH / 2)
                            ctx.setLineDash([])
                            ctx.lineWidth = 2
                            ctx.strokeStyle = n.isBus ? "#e08a3c" : "#5b7fe0"
                            curve(ctx, from, to)
                            for (var s = 0; s < n.sends.length; ++s) {
                                var sp = root.busPos(n.sends[s].busId)
                                var sto = Qt.point(sp.x, sp.y + root.nodeH / 2 + 8)
                                ctx.setLineDash([5, 4])
                                ctx.lineWidth = 1.5
                                ctx.strokeStyle = "#3cae7a"
                                curve(ctx, Qt.point(from.x, from.y + 6), sto)
                                ctx.setLineDash([])
                                ctx.fillStyle = "#3cae7a"
                                ctx.font = "11px sans-serif"
                                ctx.fillText(root.levelText(n.sends[s].level),
                                             (from.x + sto.x) / 2 - 18, (from.y + sto.y) / 2 - 4)
                            }
                        }
                        if (root.dragging) {
                            ctx.setLineDash(root.sendMode ? [5, 4] : [])
                            ctx.lineWidth = 2
                            ctx.strokeStyle = ui.theme.accentColor
                            curve(ctx, root.dragFrom, root.dragTo)
                        }
                    }
                }

                // Track and bus nodes
                Repeater {
                    model: root.allNodes()

                    delegate: Rectangle {
                        id: nodeRect

                        required property var modelData

                        readonly property point pos: root.nodePos(modelData.trackId)
                        readonly property bool selected: root.selectedTrackId === modelData.trackId

                        x: pos.x
                        y: pos.y
                        width: root.nodeW
                        height: root.nodeH
                        radius: 6
                        color: modelData.isBus ? "#4a3a2a" : ui.theme.buttonColor
                        border.width: selected ? 2 : 1
                        border.color: selected ? ui.theme.accentColor : ui.theme.strokeColor

                        StyledTextLabel {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 18
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignLeft
                            text: (modelData.isBus ? "⇶ " : "") + modelData.name
                            color: modelData.isBus ? "#f3d2b0" : ui.theme.fontPrimaryColor
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedTrackId = modelData.trackId
                        }

                        // output port: drag to a bus or Master
                        Rectangle {
                            id: port
                            width: 12
                            height: 12
                            radius: 6
                            x: parent.width - 6
                            y: parent.height / 2 - 6
                            color: ui.theme.accentColor

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.CrossCursor
                                preventStealing: true

                                onPressed: function (mouse) {
                                    var p = mapToItem(canvasArea, mouse.x, mouse.y)
                                    root.dragTrackId = modelData.trackId
                                    root.dragFrom = Qt.point(nodeRect.x + root.nodeW, nodeRect.y + root.nodeH / 2)
                                    root.dragTo = p
                                    root.dragging = true
                                    root.selectedTrackId = modelData.trackId
                                }
                                onPositionChanged: function (mouse) {
                                    root.dragTo = mapToItem(canvasArea, mouse.x, mouse.y)
                                    edges.requestPaint()
                                }
                                onReleased: function (mouse) {
                                    var target = root.dropTarget(mapToItem(canvasArea, mouse.x, mouse.y))
                                    root.dragging = false
                                    if (target !== undefined) {
                                        if (root.sendMode && target !== "") {
                                            graph.setSend(root.dragTrackId, target, 0.5, true)
                                        } else if (!root.sendMode) {
                                            graph.setOutput(root.dragTrackId, target)
                                        }
                                    }
                                    edges.requestPaint()
                                }
                            }
                        }
                    }
                }

                // Master node
                Rectangle {
                    readonly property point pos: root.masterPos()
                    x: pos.x
                    y: pos.y
                    width: root.nodeW
                    height: root.nodeH
                    radius: 6
                    color: "#2f4a35"
                    border.color: ui.theme.strokeColor

                    StyledTextLabel {
                        anchors.centerIn: parent
                        text: qsTrc("automation", "Master")
                        color: "#cfeedd"
                    }
                }

                StyledTextLabel {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    text: graph.message
                    color: "#e05b5b"
                }
            }
        }

        // ---- inspector -------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor
            radius: 4

            readonly property var node: root.selectedTrackId !== null ? root.findNode(root.selectedTrackId) : null

            Column {
                id: inspector
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                readonly property var node: parent.node

                StyledTextLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.bodyBoldFont
                    text: inspector.node ? inspector.node.name : qsTrc("automation", "Select a track or bus")
                }

                StyledTextLabel {
                    visible: !!inspector.node
                    text: qsTrc("automation", "Output")
                }

                StyledDropdown {
                    id: outputDropdown
                    visible: !!inspector.node
                    width: parent.width

                    model: {
                        var opts = [{ text: qsTrc("automation", "Master"), value: "" }]
                        if (!inspector.node) {
                            return opts
                        }
                        for (var i = 0; i < graph.buses.length; ++i) {
                            var b = graph.buses[i]
                            if (graph.canRouteTo(inspector.node.trackId, b.busId)) {
                                opts.push({ text: b.name, value: b.busId })
                            }
                        }
                        return opts
                    }
                    currentIndex: {
                        if (!inspector.node) {
                            return 0
                        }
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].value === inspector.node.output) {
                                return i
                            }
                        }
                        return 0
                    }
                    onActivated: function (index, value) {
                        graph.setOutput(inspector.node.trackId, value)
                    }
                }

                StyledTextLabel {
                    visible: !!inspector.node
                    text: qsTrc("automation", "Sends (post-fader)")
                }

                Repeater {
                    model: inspector.node ? inspector.node.sends : []

                    delegate: Column {
                        required property var modelData
                        width: inspector.width
                        spacing: 2

                        RowLayout {
                            width: parent.width
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: root.busName(modelData.busId) + "  " + root.levelText(modelData.level)
                            }
                            FlatButton {
                                text: qsTrc("automation", "Remove")
                                onClicked: graph.removeSend(inspector.node.trackId, modelData.busId)
                            }
                        }
                        StyledSlider {
                            width: parent.width
                            from: 0
                            to: 1
                            value: modelData.level
                            onMoved: graph.setSend(inspector.node.trackId, modelData.busId, value, false)
                            onPressedChanged: {
                                if (!pressed) {
                                    graph.setSend(inspector.node.trackId, modelData.busId, value, true)
                                }
                            }
                        }
                    }
                }

                StyledDropdown {
                    visible: !!inspector.node
                    width: parent.width
                    displayText: qsTrc("automation", "Add send…")
                    currentIndex: -1
                    model: {
                        var opts = []
                        if (!inspector.node) {
                            return opts
                        }
                        for (var i = 0; i < graph.buses.length; ++i) {
                            var b = graph.buses[i]
                            var already = inspector.node.sends.some(function (s) { return s.busId === b.busId })
                            if (!already && b.busId !== inspector.node.output
                                    && graph.canRouteTo(inspector.node.trackId, b.busId)) {
                                opts.push({ text: b.name, value: b.busId })
                            }
                        }
                        return opts
                    }
                    onActivated: function (index, value) {
                        graph.setSend(inspector.node.trackId, value, 0.5, true)
                    }
                }
            }
        }
    }
}
