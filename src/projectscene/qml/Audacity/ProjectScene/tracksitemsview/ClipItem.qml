import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import Muse.GraphicalEffects

import Audacity.ProjectScene
import Audacity.Playback
import Audacity.Spectrogram
import Audacity.UiComponents
import Audacity.Automation

Rectangle {
    id: root

    property alias context: waveView.context
    property alias clipKey: waveView.clipKey
    property alias clipTime: waveView.clipTime
    property alias title: titleLabel.text
    required property bool isAutomationEnabled
    required property bool isWaveformViewVisible
    required property bool isSpectrogramViewVisible
    property int pitch: 0
    property bool isPitchModified: false
    //! Non-destructive fades (seconds) drawn over the waveform
    property real fadeIn: 0
    property real fadeOut: 0
    property int fadeCurve: 0
    //! While a fade handle is dragged: the previewed lengths (seconds), otherwise -1
    property real previewFadeIn: -1
    property real previewFadeOut: -1
    property int previewFadeCurve: -1
    readonly property real shownFadeIn: previewFadeIn >= 0 ? previewFadeIn : fadeIn
    readonly property real shownFadeOut: previewFadeOut >= 0 ? previewFadeOut : fadeOut
    readonly property int shownFadeCurve: previewFadeCurve >= 0 ? previewFadeCurve : fadeCurve
    property int speedPercentage: 0
    property bool isSpeedModified: false
    property bool showChannelSplitter: false
    property alias channelHeightRatio: waveChannelSplitter.channelHeightRatio
    property var canvas: null
    required property int headerHeight
    property color clipColor: ui.theme.extra["clip_color_1"]
    property color clipSelectedColor: ui.theme.extra["clip_selected_color_1"]
    property color normalHeaderColor: root.currentClipStyle == ClipStyle.COLORFUL ? root.clipColor : root.classicHeaderColor
    property color selectedHeaderColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.3) : classicHeaderColor
    property color normalHeaderHoveredColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.8) : classicHeaderHoveredColor
    property color selectedHeaderHoveredColor: root.currentClipStyle == ClipStyle.COLORFUL ? ui.blendColors(ui.theme.extra["white_color"], root.clipColor, 0.2) : classicHeaderHoveredColor
    readonly property color classicHeaderColor: ui.theme.extra["classic_clip_header_color"]
    readonly property color classicHeaderHoveredColor: ui.theme.extra["classic_clip_header_hover_color"]
    property int currentClipStyle: ClipStyle.COLORFUL
    property bool isGrouped: false
    property bool clipSelected: false
    property bool clipIntersectsSelection: false
    property bool clipFocused: false
    property bool isDataSelected: false
    property bool isMultiSelectionActive: false
    property bool multiClipsSelected: root.isMultiSelectionActive && root.clipSelected
    property bool moveActive: false
    property bool isAudible: true
    property bool isLinear: false
    property real dbRange: -60.0
    property var displayBounds: ({
            "min": -1.0,
            "max": 1.0
        })
    property real selectionStart: 0
    property real selectionWidth: 0
    required property bool selectionInProgress
    required property bool selectionEditInProgress
    required property bool verticalSelectionEditInProgress
    property bool enableCursorInteraction: !selectionInProgress && !selectionEditInProgress && !verticalSelectionEditInProgress && !isBrush
    property bool isContrastFocusBorderEnabled: false

    required property real selectionStartFrequency
    required property real selectionEndFrequency
    required property bool spectralSelectionEnabled
    required property var pressedSpectrogram
    required property bool splitToolActive

    property real distanceToLeftNeighbor: -1
    property real distanceToRightNeighbor: -1

    property real leftVisibleMargin: 0
    property real rightVisibleMargin: 0

    property bool collapsed: false

    property bool multiSampleEdit: false

    property bool asymmetricStereoHeightsPossible: false

    signal clipStartEditRequested
    signal clipEndEditRequested
    signal cancelClipDragEditRequested

    signal clipLeftTrimRequested(bool completed, int action)
    signal clipRightTrimRequested(bool completed, int action)
    signal clipLeftStretchRequested(bool completed, int action)
    signal clipRightStretchRequested(bool completed, int action)

    signal requestSelected
    signal requestSelectionReset
    signal splitterPositionChangeRequested(int position)

    signal pitchChangeRequested
    signal pitchResetRequested

    signal speedChangeRequested
    signal speedResetRequested

    signal titleEditStarted
    signal titleEditAccepted(var newTitle)
    signal titleEditCanceled

    signal startAutoScroll
    signal stopAutoScroll

    // mouse position event is not propagated on overlapping mouse areas
    // so we are handling it manually
    signal clipItemMousePositionChanged(real x, real y)

    property alias navigation: navCtrl

    radius: 4
    color: clipSelected ? clipSelectedColor : clipColor
    border.color: ui.theme.extra["black_color"]
    opacity: root.moveActive && (clipSelected || clipIntersectsSelection) ? 0.5 : isAudible ? 1.0 : 0.3

    onMoveActiveChanged: {
        root.parent.z = moveActive && (clipSelected || clipIntersectsSelection) ? 1 : 0
    }

    property int borderWidth: 1
    property bool hover: clipHoverHandler.hovered || root.containsMouse || root.headerHovered
    property bool headerHovered: false
    property var lastSample: undefined
    property bool altPressed: false
    property bool isBrush: waveView.isStemPlot && root.altPressed
    property bool isIsolationMode: false
    property bool containsMouse: false
    property bool automationMouseHoverLetThrough: false
    property alias isNearSample: waveView.isNearSample
    property alias currentChannel: waveView.currentChannel
    property bool leftTrimContainsMouse: false
    property bool rightTrimContainsMouse: false
    property alias leftTrimPressedButtons: leftTrimStretchEdgeHover.pressedButtons
    property alias rightTrimPressedButtons: rightTrimStretchEdgeHover.pressedButtons
    property bool enableFocusBorder: (navCtrl ? navCtrl.highlight : false) || root.clipSelected
    property bool enableDefaultBorder: !isContrastFocusBorderEnabled && enableFocusBorder
    property bool enableContrastBorder: isContrastFocusBorderEnabled && enableFocusBorder

    PlaybackStateModel {
        id: playbackState
    }

    ClipGainModel {
        id: clipGainModel

        clipKey: root.clipKey
    }

    // for navigating between clips
    NavigationControl {
        id: navCtrl
        name: root.name
        enabled: root.enabled && root.visible

        accessible.role: MUAccessible.Button
        accessible.name: qsTrc("projectscene", "Clip: %1").arg(root.title)

        onActiveChanged: function (active) {
            // Make sure the focus navigation border is visible on top of other clips
            root.parent.z = active ? 1 : 0
            if (active) {
                root.forceActiveFocus()
            }
        }

        onTriggered: {
            clipNavigationPanel.requestActive()
        }
    }

    Rectangle {
        id: borderRect

        anchors.fill: parent
        color: "transparent"
        border.width: root.enableContrastBorder ? 2 : 1
        border.color: ui.theme.extra["black_color"]
        radius: root.enableContrastBorder ? 0 : 4
        z: root.parent.z + 1
    }

    NavigationFocusBorder {
        id: focusBorder

        navigationCtrl: navCtrl

        border.color: ui.theme.fontPrimaryColor
        border.width: 2
        radius: 4

        visible: root.enableDefaultBorder
    }

    NavigationFocusBorder {
        id: contrastFocusBorder

        navigationCtrl: navCtrl

        border.color: ui.theme.extra["white_color"]
        border.width: 2
        radius: 4

        visible: root.enableContrastBorder
    }

    QtObject {
        id: prv

        readonly property int doubleClickInterval: 400
        readonly property int doubleClickMaxDistance: 5

        property bool singleMenuLoaded: false
        property bool multiMenuLoaded: false

        function ensureSingleMenuLoaded() {
            if (!singleMenuLoaded) {
                singleClipContextMenuModel.load()
                singleMenuLoaded = true
            }
        }

        function ensureMultiMenuLoaded() {
            if (!multiMenuLoaded) {
                multiClipContextMenuModel.load()
                multiMenuLoaded = true
            }
        }
    }

    // panel for navigating within the clip's items
    property NavigationPanel clipNavigationPanel: NavigationPanel {
        name: "ClipNavigationPanel"
        enabled: navCtrl.active
        direction: NavigationPanel.Horizontal
        section: navigation.panel ? navigation.panel.section : null
        onActiveChanged: function (active) {
            if (active) {
                root.forceActiveFocus()
            }
        }

        onNavigationEvent: function (event) {
            if (event.type === NavigationEvent.Escape && !clipHandles.leftTrimActive && !clipHandles.rightTrimActive && !clipHandles.leftStretchActive && !clipHandles.rightStretchActive) {
                navCtrl.requestActive()
            }
        }
    }

    onAsymmetricStereoHeightsPossibleChanged: {
        if (!asymmetricStereoHeightsPossible) {
            root.ratioChanged(showChannelSplitter ? 0.5 : 1)
        }
    }

    function editTitle() {
        editLoader.edit(titleLabel.text)
    }

    function acceptEditTitle(newTitle) {
        Qt.callLater(root.titleEditAccepted, newTitle)
    }

    function openContextMenu() {
        if (root.multiClipsSelected || root.isGrouped) {
            prv.ensureMultiMenuLoaded()
        } else {
            prv.ensureSingleMenuLoaded()
        }
        menuBtn.toggleMenu(menuBtn)
    }

    function openContextMenuAt(x, y, modifiers) {
        if (!root.enableCursorInteraction) {
            return
        }

        if (root.multiClipsSelected) {
            prv.ensureMultiMenuLoaded()
            if (modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) {
                if (!root.clipSelected) {
                    root.requestSelectionReset()
                }
                root.requestSelected()
            }
            multiClipContextMenuLoader.show(Qt.point(x, y), multiClipContextMenuModel.items)
        } else if (root.isGrouped) {
            prv.ensureMultiMenuLoaded()
            if (!root.clipSelected) {
                root.requestSelectionReset()
            }
            root.requestSelected()
            multiClipContextMenuLoader.show(Qt.point(x, y), multiClipContextMenuModel.items)
        } else {
            prv.ensureSingleMenuLoaded()
            singleClipContextMenuLoader.show(Qt.point(x, y), singleClipContextMenuModel.items)
            root.requestSelected()
        }
    }

    function mousePositionChanged(x, y) {
        clipItemMousePositionChanged(x, y)
        waveView.onWaveViewPositionChanged(x, y - header.height)
    }

    function mousePressAndHold(x, y) {
        root.altPressed ? waveView.smoothLastClickPos(x, y - header.height) : waveView.setLastClickPos(x, y - header.height, x, y - header.height)
        waveView.update()
    }

    function mouseReleased() {
        waveView.isNearSample = false
        waveView.onWaveViewPositionChanged(lastSample.x, lastSample.y)
    }

    function getSpectrogramHit(y /* relative to tracks canvas */) {
        return spectrogramViewLoader.item ? spectrogramViewLoader.item.getSpectrogramHit(y) : null
    }

    function setLastSample(x, y) {
        lastSample = {
            x: x,
            y: y - header.height
        }
    }

    function setContainsMouse(containsMouse) {
        if (!root.enableCursorInteraction) {
            return
        }

        root.containsMouse = containsMouse || root.automationMouseHoverLetThrough
        if (!root.containsMouse && !root.multiSampleEdit) {
            waveView.isNearSample = false
        }
    }

    function updateViews() {
        waveView.update()
        if (spectrogramViewLoader.item) {
            spectrogramViewLoader.item.update()
        }
    }

    ClipContextMenuModel {
        id: singleClipContextMenuModel
        clipKey: root.clipKey

        onClipTitleEditRequested: {
            root.editTitle()
        }
    }

    ContextMenuLoader {
        id: singleClipContextMenuLoader

        onHandleMenuItem: function (itemId) {
            singleClipContextMenuModel.handleMenuItem(itemId)
        }
    }

    MultiClipContextMenuModel {
        id: multiClipContextMenuModel
    }

    ContextMenuLoader {
        id: multiClipContextMenuLoader

        onHandleMenuItem: function (itemId) {
            multiClipContextMenuModel.handleMenuItem(itemId)
        }
    }

    Component.onCompleted: {
        playbackState.init()
        clipGainModel.init()
    }

    Component.onDestruction: {
        //! NOTE The outer component uses this information to handle the current cursor.
        // It is important to cleanup this state before removing the component
        // to prevent the cursor from being stuck in the wrong state.
        waveView.isNearSample = false
        waveView.isStemPlot = false
        root.leftTrimContainsMouse = false
        root.rightTrimContainsMouse = false
        root.headerHovered = false
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent

        hoverEnabled: true

        readonly property bool forbidden: (root.isNearSample || root.isIsolationMode) && playbackState.isPlaying
        cursorShape: forbidden ? Qt.ForbiddenCursor : Qt.BlankCursor

        function updateCustomCursor() {
            if (!forbidden) {
                CustomCursorProvider.setCursorShape(hoverArea, ":/images/customCursorShapes/IBeamCursor.png", 26)
            }
        }

        Component.onCompleted: updateCustomCursor()
        onForbiddenChanged: updateCustomCursor()

        acceptedButtons: Qt.NoButton

        visible: root.enableCursorInteraction

        onVisibleChanged: {
            root.setContainsMouse(containsMouse)
        }

        onPositionChanged: function (e) {
            clipItemMousePositionChanged(e.x, e.y)

            // propagate mouse position to the wave view adjusting the y position
            waveView.onWaveViewPositionChanged(e.x, e.y - header.height)
        }

        onContainsMouseChanged: {
            if (!root.visible) {
                return
            }

            root.setContainsMouse(containsMouse)
        }
    }

    HoverHandler {
        id: clipHoverHandler

        enabled: root.enableCursorInteraction
    }

    // NOTE: hover events from polyline are not visible in MouseArea
    // so we need to handle them manually
    HoverHandler {
        id: passiveHoverHandler

        enabled: root.isAutomationEnabled

        onHoveredChanged: {
            automationMouseHoverLetThrough = hovered
            root.setContainsMouse(hoverArea.containsMouse)
        }
    }

    MouseArea {
        id: leftTrimStretchEdgeHover

        x: distanceToLeftNeighbor >= -0.5 && distanceToLeftNeighbor <= 10 ? root.x - Math.min(distanceToLeftNeighbor / 2, 5) : root.x - 5
        width: distanceToLeftNeighbor >= -0.5 && distanceToLeftNeighbor <= 10 ? 6 + Math.min(distanceToLeftNeighbor / 2, 5) : 11
        z: headerDragArea.z + 1
        height: !root.collapsed ? root.height / 3 : root.height / 2

        anchors.top: root.top

        hoverEnabled: true
        visible: !root.clipSelected && root.enableCursorInteraction

        cursorShape: Qt.BlankCursor

        // make sure cursor is visible on top of nearby clips
        onContainsMouseChanged: {
            root.leftTrimContainsMouse = containsMouse
            if (containsMouse || pressedButtons) {
                root.parent.z = 1
            } else {
                root.parent.z = 0
            }
        }

        onPressed: function (e) {
            root.clipStartEditRequested()
        }

        onReleased: function (e) {
            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                root.clipLeftStretchRequested(true, ClipBoundaryAction.Shrink)
            } else {
                root.clipLeftTrimRequested(true, ClipBoundaryAction.Shrink)
            }

            root.stopAutoScroll()
            // this needs to be always at the very end
            root.clipEndEditRequested()
        }

        onPositionChanged: function (e) {
            let mousePos = mapToItem(root, e.x, e.y)
            clipItemMousePositionChanged(mousePos.x, mousePos.y)

            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                if (pressed) {
                    root.clipLeftStretchRequested(false, ClipBoundaryAction.Shrink)
                }
            } else {
                if (pressed) {
                    root.clipLeftTrimRequested(false, ClipBoundaryAction.Shrink)
                }
            }
        }

        onCanceled: e => {
            root.cancelClipDragEditRequested()
        }
    }

    MouseArea {
        id: rightTrimStretchEdgeHover

        x: root.width - 5
        z: headerDragArea.z + 1
        width: distanceToRightNeighbor >= -0.5 && distanceToRightNeighbor <= 10 ? 6 + Math.min(distanceToRightNeighbor / 2, 5) : 11
        height: !root.collapsed ? root.height / 3 : root.height / 2

        anchors.top: root.top

        hoverEnabled: true
        visible: !root.clipSelected && root.enableCursorInteraction

        cursorShape: Qt.BlankCursor

        // make sure cursor is visible on top of nearby clips
        onContainsMouseChanged: {
            root.rightTrimContainsMouse = containsMouse
            if (containsMouse || pressedButtons) {
                root.parent.z = 1
            } else {
                root.parent.z = 0
            }
        }

        onPressed: function (e) {
            root.clipStartEditRequested()
        }

        onReleased: function (e) {
            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                root.clipRightStretchRequested(true, ClipBoundaryAction.Shrink)
            } else {
                root.clipRightTrimRequested(true, ClipBoundaryAction.Shrink)
            }

            root.stopAutoScroll()
            // this needs to be always at the very end
            root.clipEndEditRequested()
        }

        onPositionChanged: function (e) {
            let mousePos = mapToItem(root, e.x, e.y)
            clipItemMousePositionChanged(mousePos.x, mousePos.y)

            if (e.modifiers & (Qt.AltModifier | Qt.MetaModifier)) {
                if (pressed) {
                    root.clipRightStretchRequested(false, ClipBoundaryAction.Shrink)
                }
            } else {
                if (pressed) {
                    root.clipRightTrimRequested(false, ClipBoundaryAction.Shrink)
                }
            }
        }

        onCanceled: e => {
            root.cancelClipDragEditRequested()
        }
    }

    Rectangle {
        id: inner

        anchors.fill: parent

        layer.enabled: true
        layer.effect: RoundedCornersEffect {
            radius: root.radius
        }

        Rectangle {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            height: root.headerHeight
            z: 2

            visible: !root.collapsed || root.hover || menuBtn.isMenuOpened

            Rectangle {
                id: headerSelectionRectangle

                x: root.selectionStart
                z: 0 // Ensure this is below the header content
                width: Math.min(root.selectionWidth, header.width)

                anchors.top: header.top
                anchors.bottom: header.bottom

                color: waveView.transformColor(clipColor)
                visible: root.isDataSelected && currentClipStyle == ClipStyle.COLORFUL
            }

            MouseArea {
                id: headerDragArea
                anchors.fill: parent

                onContainsMouseChanged: {
                    if (!root.visible) {
                        return
                    }

                    root.headerHovered = containsMouse
                }

                // during dragging, the clip is hidden, and do not receive mouse events
                // we need to restore hover state when the clip become visible again
                onVisibleChanged: {
                    root.headerHovered = containsMouse
                }

                visible: root.enableCursorInteraction

                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                cursorShape: root.splitToolActive ? Qt.ArrowCursor : Qt.OpenHandCursor

                property var lastClickTime: 0
                property point doubleClickStartPosition

                //! IMPORTANT NOTE: clip moving is handled in TracksItemsView (because
                // Clip UI element will be destroyed along with its MouseArea if the clip is moved
                // to the other track and we're gonna loose the ability to handle this MouseArea's events)
                // hence we need to let simple events pass (e.accepted = false). Unfortunately this breaks
                // detecting composed events like doubleClick so we need to take care of it manually.
                onPressed: function (e) {
                    if (root.splitToolActive) {
                        //! NOTE Let the press through so the split tool handles it
                        e.accepted = false
                        return
                    }

                    var currentTime = Date.now()
                    if (currentTime - lastClickTime < prv.doubleClickInterval) {
                        //! NOTE Handle doubleClick logic
                        root.editTitle()
                    } else {
                        //! NOTE Handle singleClick logic
                        if ((!root.multiClipsSelected || (e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) && !(root.isDataSelected && isWithinRange(e.x, headerSelectionRectangle.x, headerSelectionRectangle.width))) {
                            root.requestSelected()
                        }

                        lastClickTime = currentTime
                        doubleClickStartPosition = Qt.point(e.x, e.y)
                    }

                    e.accepted = false
                }

                onPositionChanged: function (e) {
                    // Reset double click timer if the mouse has moved,
                    // to prevent rapid clip movement activate title editing
                    if (Math.abs(e.x - doubleClickStartPosition.x) > prv.doubleClickMaxDistance || Math.abs(e.y - doubleClickStartPosition.y) > prv.doubleClickMaxDistance) {
                        lastClickTime = 0
                    }

                    root.clipItemMousePositionChanged(e.x, e.y)

                    e.accepted = false
                }

                onReleased: function (e) {
                    e.accepted = false
                }

                function isWithinRange(val, x, w) {
                    return val >= x && val <= (x + w)
                }
            }

            StyledTextLabel {
                id: titleLabel
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: buttonsRow.left
                anchors.leftMargin: root.leftVisibleMargin + 4
                anchors.rightMargin: 8
                horizontalAlignment: Qt.AlignLeft

                NavigationControl {
                    id: titleEditNavCtrl
                    name: "TitleEditNavCtrl"
                    enabled: root.enabled && root.visible
                    panel: root.clipNavigationPanel
                    column: 3

                    accessible.role: MUAccessible.EditableText
                    accessible.name: qsTrc("projectscene", "Clip name: %1").arg(root.title)
                    accessible.enabled: titleEditNavCtrl.enabled

                    onTriggered: {
                        root.editTitle()
                    }
                }

                NavigationFocusBorder {
                    navigationCtrl: titleEditNavCtrl

                    anchors.topMargin: 1
                    anchors.bottomMargin: 0
                    radius: 5
                }
            }

            Loader {
                id: editLoader

                anchors.fill: titleLabel

                property bool isEditState: false
                sourceComponent: editLoader.isEditState ? titleEditComp : null

                function edit(text) {
                    root.titleEditStarted()
                    editLoader.isEditState = true
                    editLoader.item.currentText = text
                    editLoader.item.newTitle = text
                    editLoader.item.visible = true
                    editLoader.item.ensureActiveFocus()
                }
            }

            Component {
                id: titleEditComp

                TextInputField {
                    id: titleEdit

                    property string newTitle: ""

                    anchors.fill: parent
                    background.color: header.color
                    background.border.width: 0
                    background.radius: 0
                    inputField.color: titleLabel.color
                    textSidePadding: 0
                    visible: false

                    onTextChanged: function (text) {
                        titleEdit.newTitle = text
                    }

                    onAccepted: {
                        root.acceptEditTitle(titleEdit.newTitle)
                        editLoader.isEditState = false
                    }

                    onEscaped: {
                        editLoader.isEditState = false
                    }

                    onFocusChanged: {
                        if (!titleEdit.focus) {
                            titleEdit.visible = false
                            titleEdit.accepted()
                        }
                    }
                }
            }

            Row {
                id: buttonsRow

                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter

                spacing: 2

                ClipItemPropertyButton {
                    id: pitchBtn

                    mouseArea.visible: root.enableCursorInteraction

                    text: {
                        let semis = Math.trunc(root.pitch / 100)
                        let cents = Math.abs(root.pitch % 100).toString().padStart(2, "0")
                        return semis + (cents > "00" ? "." + cents : "")
                    }
                    icon: root.pitch > 0 ? IconCode.ARROW_UP : IconCode.ARROW_DOWN

                    visible: root.isPitchModified && header.width > (60 + pitchBtn.implicitWidth + speedBtn.implicitWidth * (root.isSpeedModified ? 1 : 0) + menuBtn.implicitWidth)

                    onClicked: function (mouse) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            root.pitchResetRequested()
                        } else {
                            root.pitchChangeRequested()
                        }
                    }
                }

                ClipItemPropertyButton {
                    id: speedBtn

                    mouseArea.visible: root.enableCursorInteraction

                    icon: IconCode.CLOCK
                    text: root.speedPercentage + "%"

                    visible: root.isSpeedModified && header.width > (60 + speedBtn.implicitWidth + menuBtn.implicitWidth)

                    onClicked: function (mouse) {
                        if (mouse.modifiers & Qt.ControlModifier) {
                            root.speedResetRequested()
                        } else {
                            root.speedChangeRequested()
                        }
                    }
                }

                MenuButton {
                    id: menuBtn
                    width: 16
                    height: 16
                    anchors.verticalCenter: parent.verticalCenter

                    mouseArea.visible: root.enableCursorInteraction

                    menuModel: (root.multiClipsSelected || root.isGrouped) ? multiClipContextMenuModel : singleClipContextMenuModel

                    accentColor: root.clipColor
                    hoverHitColor: root.clipSelectedColor

                    visible: header.width > (60 + menuBtn.implicitWidth)

                    navigation.name: "ClipMenuBtn"
                    navigation.panel: root.clipNavigationPanel
                    navigation.column: 4
                    navigation.accessible.name: qsTrc("projectscene", "Clip menu")

                    onHandleMenuItem: function (itemId) {
                        Qt.callLater(menuModel.handleMenuItem, itemId)
                    }

                    //! NOTE: Override doClicked function from FlatButton to run clip selection logic before the base
                    //! menu button emits clicked
                    function doClicked(mouse) {
                        if (root.multiClipsSelected || root.isGrouped) {
                            prv.ensureMultiMenuLoaded()
                        } else {
                            prv.ensureSingleMenuLoaded()
                        }

                        if (!root.multiClipsSelected || (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) {
                            if (!root.clipSelected) {
                                root.requestSelectionReset()
                            }
                            root.requestSelected()
                        }

                        Qt.callLater(menuBtn.clicked, mouse)
                    }
                }
            }
        }

        Canvas {
            id: fadeOverlay

            //! Shades the part of the clip a fade attenuates and draws its gain curve
            anchors.fill: viewsColumn
            z: viewsColumn.z + 1
            visible: !root.collapsed && (root.shownFadeIn > 0 || root.shownFadeOut > 0)
            enabled: false

            readonly property real pxPerSec: root.context ? root.context.zoom : 0
            //! The item only spans the visible part of long clips: where the real clip edges are
            readonly property real clipStartPx: (waveView.startTime - waveView.itemStartTime) * pxPerSec
            readonly property real clipEndPx: (waveView.endTime - waveView.itemStartTime) * pxPerSec
            onClipStartPxChanged: requestPaint()
            onClipEndPxChanged: requestPaint()
            readonly property real inWidth: Math.min(clipEndPx - clipStartPx, root.shownFadeIn * pxPerSec)
            readonly property real outWidth: Math.min(clipEndPx - clipStartPx, root.shownFadeOut * pxPerSec)

            onInWidthChanged: requestPaint()
            onOutWidthChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Connections {
                target: root
                function onShownFadeCurveChanged() { fadeOverlay.requestPaint() }
            }

            function gain(x) {
                return root.shownFadeCurve === 1 ? Math.sin(x * Math.PI / 2) : x
            }

            function drawFade(ctx, x0, w, rising) {
                if (w < 1) {
                    return
                }
                const steps = Math.max(2, Math.ceil(w / 2))
                ctx.beginPath()
                ctx.moveTo(x0, 0)
                for (let i = 0; i <= steps; ++i) {
                    const f = i / steps
                    const g = gain(rising ? f : 1 - f)
                    ctx.lineTo(x0 + f * w, height * (1 - g))
                }
                ctx.lineTo(x0 + w, 0)
                ctx.closePath()
                ctx.fillStyle = Qt.rgba(0, 0, 0, 0.28)
                ctx.fill()

                ctx.beginPath()
                for (let i = 0; i <= steps; ++i) {
                    const f = i / steps
                    const g = gain(rising ? f : 1 - f)
                    const px = x0 + f * w, py = height * (1 - g)
                    if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                }
                ctx.lineWidth = 1.5
                ctx.strokeStyle = "#ffffff"
                ctx.stroke()
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                drawFade(ctx, clipStartPx, inWidth, true)
                drawFade(ctx, clipEndPx - outWidth, outWidth, false)
            }
        }

        Repeater {
            //! Draggable fade handles: small squares at the top corners of the clip body, sitting at
            //! the end of the fade-in / start of the fade-out. Drag horizontally to set the length
            //! (live preview, one undo step on release); hold Shift on release to toggle the curve.
            //! The Fades submenu of the clip context menu is the keyboard/accessible alternative.
            model: root.collapsed || !root.isWaveformViewVisible ? 0 : 2

            delegate: Rectangle {
                id: fadeHandle

                required property int index
                readonly property bool isIn: index === 0
                readonly property bool active: handleArea.containsMouse || handleArea.pressed

                readonly property real fadePx: (isIn ? root.shownFadeIn : root.shownFadeOut) * fadeOverlay.pxPerSec
                readonly property real size: 10

                readonly property real centre: isIn ? fadeOverlay.clipStartPx + fadePx : fadeOverlay.clipEndPx - fadePx
                readonly property bool onScreen: centre >= -size && centre <= viewsColumn.width + size
                x: isIn ? Math.max(fadeOverlay.clipStartPx + 1, Math.min(fadeOverlay.clipEndPx - size - 1, centre - size / 2))
                        : Math.min(fadeOverlay.clipEndPx - size - 1, Math.max(fadeOverlay.clipStartPx + 1, centre - size / 2))
                y: viewsColumn.y + 1
                z: fadeOverlay.z + 10
                width: size
                height: size
                radius: 2
                visible: onScreen && (root.hover || handleArea.containsMouse || handleArea.pressed || (isIn ? root.fadeIn > 0 : root.fadeOut > 0) && root.clipSelected)
                color: active ? "#ffffff" : Qt.rgba(1, 1, 1, 0.75)
                border.color: Qt.rgba(0, 0, 0, 0.6)
                border.width: 1

                Rectangle {
                    //! Length readout shown next to the handle while dragging
                    visible: handleArea.pressed && dragReadout.text !== ""
                    x: fadeHandle.isIn ? fadeHandle.size + 4 : -width - 4
                    y: -2
                    width: dragReadout.implicitWidth + 10
                    height: dragReadout.implicitHeight + 4
                    radius: 3
                    color: Qt.rgba(0, 0, 0, 0.78)

                    Text {
                        id: dragReadout
                        anchors.centerIn: parent
                        color: "#ffffff"
                        font.pixelSize: 11
                        text: ""
                    }
                }

                MouseArea {
                    id: handleArea

                    anchors.fill: parent
                    anchors.margins: -3
                    hoverEnabled: true
                    preventStealing: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.SizeHorCursor

                    property real pressX: 0
                    property real startIn: 0
                    property real startOut: 0

                    onPressed: function (e) {
                        pressX = mapToItem(root, e.x, e.y).x
                        startIn = root.fadeIn
                        startOut = root.fadeOut
                        root.previewFadeIn = root.fadeIn
                        root.previewFadeOut = root.fadeOut
                        root.previewFadeCurve = root.fadeCurve
                        dragReadout.text = singleClipContextMenuModel.fadeDragReadout(fadeHandle.isIn,
                                                                                      fadeHandle.isIn ? root.fadeIn : root.fadeOut,
                                                                                      root.fadeCurve, false)
                        e.accepted = true
                    }

                    onPositionChanged: function (e) {
                        if (!pressed || fadeOverlay.pxPerSec <= 0) {
                            return
                        }
                        const dxSec = (mapToItem(root, e.x, e.y).x - pressX) / fadeOverlay.pxPerSec
                        let f = singleClipContextMenuModel.fadeDragPreview(fadeHandle.isIn, startIn, startOut, dxSec)
                        // Snap the fade edge to the grid when project snap is on; Ctrl (Cmd on macOS) bypasses
                        if (root.context && !(e.modifiers & Qt.ControlModifier)) {
                            const edge = fadeHandle.isIn ? waveView.startTime + f.x : waveView.endTime - f.y
                            const snapped = root.context.applySnapToTime(edge)
                            if (snapped !== edge) {
                                f = singleClipContextMenuModel.fadeDragSnap(fadeHandle.isIn, f.x, f.y, snapped)
                            }
                        }
                        root.previewFadeIn = f.x
                        root.previewFadeOut = f.y
                        root.previewFadeCurve = (e.modifiers & Qt.ShiftModifier) ? (root.fadeCurve === 1 ? 0 : 1) : root.fadeCurve
                        dragReadout.text = singleClipContextMenuModel.fadeDragReadout(fadeHandle.isIn,
                                                                                      fadeHandle.isIn ? f.x : f.y,
                                                                                      root.previewFadeCurve,
                                                                                      (e.modifiers & Qt.ShiftModifier) !== 0)
                        e.accepted = true
                    }

                    // Fully consume the gesture so the clip/track below never sees it
                    // (no playhead seek, no selection change)
                    onClicked: function (e) { e.accepted = true }
                    onDoubleClicked: function (e) { e.accepted = true }
                    onPressAndHold: function (e) { e.accepted = true }

                    onReleased: function (e) {
                        e.accepted = true
                        dragReadout.text = ""
                        const toggle = (e.modifiers & Qt.ShiftModifier) !== 0
                        const fin = root.previewFadeIn, fout = root.previewFadeOut
                        root.previewFadeIn = -1
                        root.previewFadeOut = -1
                        root.previewFadeCurve = -1
                        singleClipContextMenuModel.applyFadeDrag(fin, fout, root.fadeCurve, toggle)
                    }

                    onCanceled: {
                        dragReadout.text = ""
                        root.previewFadeIn = -1
                        root.previewFadeOut = -1
                        root.previewFadeCurve = -1
                    }
                }
            }
        }

        ColumnLayout {
            id: viewsColumn

            anchors.top: (!root.collapsed && header.visible) ? header.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            spacing: 0

            WaveView {
                id: waveView
                visible: root.isWaveformViewVisible

                Layout.fillWidth: true
                Layout.fillHeight: true

                fillColor: waveView.backgroundColor

                channelHeightRatio: showChannelSplitter ? root.channelHeightRatio : 1

                clipColor: root.clipColor
                clipSelectedColor: root.clipSelectedColor
                clipSelected: root.clipSelected
                isIsolationMode: root.isIsolationMode
                multiSampleEdit: root.multiSampleEdit
                isBrush: root.isBrush
                isLinear: root.isLinear
                dbRange: root.dbRange
                displayBounds: root.displayBounds

                function onWaveViewPositionChanged(x, y) {
                    if (waveView.isIsolationMode) {
                        waveView.setIsolatedPoint(x, y)
                        waveView.update()
                    } else if (root.multiSampleEdit && !root.altPressed) {
                        var lastX = root.lastSample.x
                        var lastY = root.lastSample.y
                        waveView.setLastClickPos(lastX, lastY, x, y)
                        waveView.update()
                    } else {
                        waveView.setLastMousePos(x, y)
                    }
                }

                ChannelSplitter {
                    id: waveChannelSplitter

                    anchors.fill: parent

                    visible: root.showChannelSplitter

                    editable: root.enableCursorInteraction && root.asymmetricStereoHeightsPossible
                    asymmetricStereoHeightsPossible: root.asymmetricStereoHeightsPossible

                    color: ui.theme.extra["black_color"]
                    opacity: 0.10

                    onPositionChangeRequested: function (position) {
                        root.splitterPositionChangeRequested(position)
                    }
                }

                onIsNearSampleChanged: {
                    if (root.isNearSample) {
                        waveView.forceActiveFocus()
                    }
                }

                onIsIsolationModeChanged: {
                    if (waveView.isIsolationMode) {
                        waveView.forceActiveFocus()
                    }
                }

                onIsStemPlotChanged: {
                    if (waveView.isStemPlot && hoverArea.containsMouse) {
                        // force mouse position update will update isNearSample
                        waveView.onWaveViewPositionChanged(hoverArea.mouseX, hoverArea.mouseY - header.height)
                    }
                }

                PolylinePlot {
                    id: automation

                    anchors.fill: waveView
                    anchors.bottomMargin: 1

                    visible: root.isAutomationEnabled

                    lineColor: ui.theme.extra["audio_envelope_line"]
                    lineWidth: 2

                    points: clipGainModel.points

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

                    defaultValue: clipGainModel.defaultValue

                    // Offset the envelope display to follow the drag preview while its point times remain unchanged.
                    readonly property real timeOffset: waveView.startTime - clipGainModel.clipStartTime
                    xRangeFrom: waveView.itemStartTime - timeOffset
                    xRangeTo: waveView.itemEndTime - timeOffset

                    yRangeFrom: clipGainModel.minValue
                    yRangeTo: clipGainModel.maxValue
                    ySplitNormalized: clipGainModel.ySplitNormalized
                    ySplitValue: clipGainModel.ySplitValue
                    yAxisInverse: false

                    Component.onCompleted: {
                        automation.init()
                    }

                    onPointMoved: function (index, x, y, completed) {
                        clipGainModel.setPoint(index, x, y, completed)
                        tooltip.value = gainToDb(y)
                        tooltip.show(true)
                    }

                    onPointAdded: function (x, y, completed) {
                        clipGainModel.addPoint(x, y, completed)
                    }

                    onPointRemoved: function (index, completed) {
                        clipGainModel.removePoint(index, completed)
                    }

                    onDragCancelled: {
                        clipGainModel.cancelDrag()
                        tooltip.hide(true)
                    }

                    onInteractionFinished: function () {
                        if (!automation.hasActivePoint) {
                            tooltip.hide(true)
                        }
                    }

                    onActivePointChanged: {
                        if (automation.hasActivePoint) {
                            fake.x = automation.activePointX
                            fake.y = automation.activePointY - (automation.standardPointStyle.centerRadius + 2)
                            tooltip.value = gainToDb(automation.activePointValue)
                            tooltip.show(true)
                        } else {
                            tooltip.hide(true)
                        }
                    }

                    Item {
                        // NOTE: fakeItem for tooltip to follow
                        id: fake

                        height: 1
                        width: 1

                        x: automation.activePointX
                        y: automation.activePointY - (automation.standardPointStyle.centerRadius + 2)

                        enabled: false // so it doesn't steal mouse events

                        ValueTooltip {
                            id: tooltip

                            unitText: "dB"
                            sizingText: "-60.0dB"
                        }
                    }

                    function gainToDb(g) {
                        if (g < 0.001)  // -60 dB
                            return "-∞"

                        let db = 20 * Math.log10(g)
                        return db.toFixed(1)
                    }
                }
            }

            Loader {
                id: spectrogramViewLoader

                active: root.isSpectrogramViewVisible
                visible: active

                Layout.fillWidth: true
                Layout.fillHeight: true

                sourceComponent: ClipSpectrogramView {
                    canvas: root.canvas
                    clipId: root.clipKey.itemId()
                    trackId: root.clipKey.trackId()
                    selectionInProgress: root.selectionInProgress
                    selectionEditInProgress: root.selectionEditInProgress
                    verticalSelectionEditInProgress: root.verticalSelectionEditInProgress
                    spectralSelectionEnabled: root.spectralSelectionEnabled
                    pressedSpectrogram: root.pressedSpectrogram
                    isStereo: root.showChannelSplitter
                    channelHeightRatio: showChannelSplitter ? root.channelHeightRatio : 1

                    timelineIndentWidth: root.canvas.anchors.leftMargin
                    zoom: root.context.zoom
                    frameStartTime: root.context.frameStartTime
                    frameEndTime: root.context.frameEndTime
                    selectionStartTime: root.context.selectionStartTime
                    selectionEndTime: root.context.selectionEndTime
                    selectionStartFrequency: root.selectionStartFrequency
                    selectionEndFrequency: root.selectionEndFrequency
                    clipSelected: root.clipSelected

                    enabled: !root.splitToolActive

                    ChannelSplitter {
                        anchors.fill: parent

                        visible: root.showChannelSplitter

                        channelHeightRatio: parent.channelHeightRatio
                        editable: root.enableCursorInteraction && root.asymmetricStereoHeightsPossible
                        asymmetricStereoHeightsPossible: root.asymmetricStereoHeightsPossible

                        color: ui.theme.extra["black_color"]
                        opacity: 0.10

                        onPositionChangeRequested: function (position) {
                            root.splitterPositionChangeRequested(position)
                        }
                    }
                }
            }
        }

        FlatButton {
            id: accessibilitySelectBtn

            anchors.horizontalCenter: viewsColumn.horizontalCenter
            anchors.bottom: viewsColumn.bottom
            anchors.bottomMargin: 10

            navigation.name: "SelectBtn"
            navigation.panel: root.clipNavigationPanel
            navigation.column: 0

            width: 55
            height: 20
            text: !root.clipSelected ? qsTrc("clips", "Select") : qsTrc("clips", "Deselect")
            visible: root.clipNavigationPanel.highlight
            normalColor: ui.theme.extra["accessibility_clip_select_button_color"]

            onClicked: {
                if (!root.clipSelected) {
                    root.requestSelected()
                } else {
                    root.requestSelectionReset()
                }
            }
        }
    }

    // make sure clip and its handles are visible on top of nearby clips
    onClipSelectedChanged: {
        if (clipSelected) {
            root.parent.z = 1
        } else {
            root.parent.z = 0
        }
    }

    ClipHandles {
        id: clipHandles

        // +1 not to overlap with header
        y: !root.collapsed ? header.height + 1 : 0
        width: root.width
        handlesVisible: root.clipSelected && !root.moveActive
        canvas: root.canvas
        collapsed: root.collapsed
        clipHeight: root.height
        headerHeight: header.height
        altPressed: root.altPressed
        exclusionItem: root.collapsed ? menuBtn : null

        clipNavigationPanel: root.clipNavigationPanel

        onClipHandlesMousePositionChanged: function (xWithinClipHandles, yWithinClipHandles) {
            var xWithinClipItem = xWithinClipHandles
            var yWithinClipItem = header.height + 1 + yWithinClipHandles
            clipItemMousePositionChanged(xWithinClipItem, yWithinClipItem)
        }

        onClipStartEditRequested: function () {
            root.clipStartEditRequested()
        }

        onClipEndEditRequested: function () {
            root.clipEndEditRequested()
        }

        onCancelClipDragEditRequested: function () {
            root.cancelClipDragEditRequested()
        }

        onTrimLeftRequested: function (completed, action) {
            root.clipLeftTrimRequested(completed, action)
        }

        onTrimRightRequested: function (completed, action) {
            root.clipRightTrimRequested(completed, action)
        }

        onStretchLeftRequested: function (completed, action) {
            root.clipLeftStretchRequested(completed, action)
        }

        onStretchRightRequested: function (completed, action) {
            root.clipRightStretchRequested(completed, action)
        }

        onStopAutoScroll: {
            root.stopAutoScroll()
        }
    }

    state: "NORMAL"
    states: [
        State {
            name: "NORMAL"
            when: !root.clipSelected && !headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.normalHeaderColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "SELECTED"
            when: root.clipSelected && !headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.selectedHeaderColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "NORMAL_HEADER_HOVERED"
            when: !root.clipSelected && headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.normalHeaderHoveredColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        },
        State {
            name: "SELECTED_HEADER_HOVERED"
            when: root.clipSelected && headerDragArea.containsMouse
            PropertyChanges {
                target: header
                color: root.selectedHeaderHoveredColor
            }
            PropertyChanges {
                target: titleLabel
                color: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: pitchBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: speedBtn
                textColor: ui.theme.extra["black_color"]
                iconColor: ui.theme.extra["black_color"]
            }
            PropertyChanges {
                target: menuBtn
                iconColor: ui.theme.extra["black_color"]
            }
        }
    ]
}
