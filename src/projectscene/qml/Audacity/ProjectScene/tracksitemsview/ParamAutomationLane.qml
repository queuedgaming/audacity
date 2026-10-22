/*
 * Audacity: A Digital Audio Editor
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents

import Audacity.UiComponents
import Audacity.Automation

//! Plugin parameter automation lane drawn over the lower part of a track:
//! choose an effect parameter, click to add points, drag to move, double-click to delete.
Item {
    id: root

    required property var trackId
    required property var context

    readonly property bool shown: laneModel.shown

    visible: laneModel.shown

    ParamAutomationLaneModel {
        id: laneModel
        trackId: root.trackId
    }

    Component.onCompleted: {
        laneModel.init()
    }

    Rectangle {
        anchors.fill: parent
        color: ui.theme.backgroundPrimaryColor
        opacity: 0.55
        border.color: ui.theme.strokeColor
        border.width: 1
    }

    Row {
        id: header

        x: 4
        y: 2
        z: 2
        height: 24
        spacing: 8

        StyledDropdown {
            id: targetDropdown

            width: 260
            height: 22

            model: laneModel.targetOptions
            currentIndex: laneModel.currentTargetIndex
            displayText: laneModel.targetOptions.length > 0 ? currentText : qsTrc("automation", "No realtime effect on this track")
            enabled: laneModel.targetOptions.length > 0

            onActivated: function (index, value) {
                laneModel.selectTarget(index)
            }
        }

        StyledTextLabel {
            id: readout

            anchors.verticalCenter: parent.verticalCenter
            text: plot.hasActivePoint
                  ? laneModel.valueText(plot.activePointValue)
                  : qsTrc("automation", "%1 … %2 (current %3)")
                    .arg(laneModel.valueText(laneModel.minValue))
                    .arg(laneModel.valueText(laneModel.maxValue))
                    .arg(laneModel.valueText(laneModel.defaultValue))
        }
    }

    PolylinePlot {
        id: plot

        anchors.fill: parent
        anchors.topMargin: header.height + 4
        anchors.bottomMargin: 2

        enabled: laneModel.currentTargetIndex >= 0 || laneModel.points.length > 0

        lineColor: ui.theme.extra["audio_envelope_line"]
        lineWidth: 2

        points: laneModel.points
        defaultValue: laneModel.defaultValue

        standardPointStyle {
            centerRadius: 5.0
            centerColor: ui.theme.extra["audio_envelope_point"]

            centerRadiusHovered: 1.0
            centerColorHovered: ui.theme.extra["white_color"]

            middleRingWidthHovered: 2.0
            middleRingColorHovered: ui.theme.extra["black_color"]

            outlineWidthHovered: 3.0
            outlineColorHovered: ui.theme.extra["audio_envelope_point"]
        }

        ghostPointStyle {
            centerRadius: 4.0
            centerColor: ui.theme.extra["audio_envelope_point"]
        }

        xRangeFrom: root.context ? root.context.frameStartTime : 0
        xRangeTo: root.context ? root.context.frameEndTime : 1

        yRangeFrom: laneModel.minValue
        yRangeTo: laneModel.maxValue
        ySplitNormalized: 0.5
        ySplitValue: (laneModel.minValue + laneModel.maxValue) / 2
        yAxisInverse: false

        Component.onCompleted: {
            plot.init()
        }

        onPointMoved: function (index, x, y, completed) {
            laneModel.setPoint(index, x, y, completed)
        }

        onPointAdded: function (x, y, completed) {
            laneModel.addPoint(x, y, completed)
        }

        onPointRemoved: function (index, completed) {
            laneModel.removePoint(index, completed)
        }

        onDragCancelled: {
            laneModel.cancelDrag()
        }

        Item {
            id: fake

            height: 1
            width: 1
            x: plot.activePointX
            y: plot.activePointY - (plot.standardPointStyle.centerRadius + 2)
            enabled: false
            visible: plot.hasActivePoint

            ValueTooltip {
                id: tooltip
                value: laneModel.valueText(plot.activePointValue)
                sizingText: "-000.00"
            }
        }

        onActivePointChanged: {
            if (plot.hasActivePoint) {
                tooltip.show(true)
            } else {
                tooltip.hide(true)
            }
        }
    }
}
