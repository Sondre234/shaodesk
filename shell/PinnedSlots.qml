// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import Shaodesk

// A pinned application's slot shows its launcher, or its windows while it has any.
// Dragging either slides the slot along the bar, the slots it passes halfway over
// making way; dropping it keeps it where it was dragged to. An application just pinned
// fades and grows into a slot that opens for it, and a window opening in a slot draws
// its line out under the launcher's icon (or, beside the slot's other windows, comes in
// as a window's button does in the task list).
Repeater {
    id: pinnedSlots
    required property var panel
    model: shell.pinned
    // The applications whose slots are on the bar. Every change to the pins makes the slots
    // anew, so a slot comes in only when its application was not among them.
    property var shownApps: []
    property bool settled: false
    Component.onCompleted: Qt.callLater(remember)
    function remember() {
        shownApps = shell.pinned.map(function(app) { return app.appId })
        settled = true
    }
    onItemAdded: (index, item) => {
        if (settled && shownApps.indexOf(item.modelData.appId) < 0)
            item.enter()
        item.settled = true
        Qt.callLater(remember)
    }
    // The slot being dragged, the slot whose place it takes, and how far the slots
    // in between step aside.
    property int dragFrom: -1
    property int dragTo: -1
    property real dragStep: 0
    delegate: RowLayout {
        id: pinnedSlot
        required property var modelData
        required property int index
        property real dragX: 0
        readonly property bool dragging: pinnedSlots.dragFrom === index
        function drag(handler) {
            dragX = handler.activeTranslation.x
            var center = x + width / 2 + dragX, to = index
            for (var i = 0; i < pinnedSlots.count; ++i) {
                var other = pinnedSlots.itemAt(i)
                if ((i > index && center >= other.x + other.width / 2) ||
                    (i < index && center <= other.x + other.width / 2 && to === index))
                    to = i
            }
            pinnedSlots.dragStep = width + parent.spacing
            pinnedSlots.dragTo = to
            pinnedSlots.dragFrom = index
        }
        function drop() {
            var to = pinnedSlots.dragTo
            dragX = 0
            pinnedSlots.dragFrom = pinnedSlots.dragTo = -1
            // Moving the pin rebuilds the slots, this one and its drag handler with them.
            if (to >= 0 && to !== index) {
                var app = modelData.appId, target = pinnedSlots.itemAt(to).modelData.appId
                Qt.callLater(function() { shell.movePin(app, target) })
            }
        }
        readonly property real shift: {
            var from = pinnedSlots.dragFrom, to = pinnedSlots.dragTo
            if (from < 0 || index === from)
                return 0
            if (from < to && index > from && index <= to)
                return -pinnedSlots.dragStep
            if (from > to && index >= to && index < from)
                return pinnedSlots.dragStep
            return 0
        }
        property real shiftX: shift
        Behavior on shiftX { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        spacing: 4
        z: dragging ? 1 : 0
        // Made with the bar or anew as the pins changed: what is added after comes in.
        property bool settled: false
        // How far it has come in, from 0 to 1: it fades and grows in as its room opens.
        property real grow: 1
        NumberAnimation on grow {
            id: growing
            running: false
            from: 0; to: 1
            duration: Theme.durationNormal; easing.type: Theme.easing
        }
        function enter() { growing.restart() }
        opacity: grow
        scale: Theme.growFrom + (1 - Theme.growFrom) * grow
        Layout.preferredWidth: implicitWidth * grow
        // The transform's own x, not the item's: it does not fight the layout.
        // qmllint disable Quick.layout-positioning
        transform: Translate { x: pinnedSlot.dragging ? pinnedSlot.dragX : pinnedSlot.shiftX }
        // qmllint enable Quick.layout-positioning
        FlatButton {
            id: pinnedButton
            objectName: "pinned:" + pinnedSlot.modelData.appId
            visible: pinnedTasks.count === 0
            Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
            // Padded like a window's button, so the icon stays put when one opens.
            topPadding: Theme.spacingXS; bottomPadding: Theme.spacingS + Theme.spacingXS
            onClicked: { if (shell.launch(pinnedSlot.modelData.appId)) pinnedSlots.panel.closeMenus() }
            Accessible.name: pinnedSlot.modelData.name
            contentItem: Item {
                BarAppIcon {
                    anchors.centerIn: parent
                    name: pinnedSlot.modelData.icon
                    pressed: pinnedButton.pressed
                    size: Math.min(Theme.appIconSize, parent.height)
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onPressed: pinnedSlots.panel.openContextMenu(pinnedButton, 0, -1, pinnedSlot.modelData)
            }
            DragHandler {
                target: null
                yAxis.enabled: false
                onActiveTranslationChanged: if (active) pinnedSlot.drag(this)
                onActiveChanged: if (!active) pinnedSlot.drop()
            }
        }
        // Grouped, the slot's first window stands for all of them.
        TaskFilter { id: pinnedWindows; controller: shell; app: pinnedSlot.modelData.appId; sourceModel: pinnedSlots.panel.taskSource }
        Repeater {
            model: TaskFilter { id: pinnedTasks; controller: shell; app: pinnedSlot.modelData.appId; sourceModel: pinnedSlots.panel.taskSource; grouped: shell.groupWindows }
            // The first window takes the launcher's place, the others come in beside it.
            onItemAdded: (index, item) => {
                if (pinnedSlot.settled) {
                    if (pinnedTasks.count === 1) item.appear(); else item.enter()
                }
            }
            delegate: TaskButton {
                objectName: "pinnedTask:" + pinnedSlot.modelData.appId
                panel: pinnedSlots.panel
                iconName: pinnedSlot.modelData.icon
                group: shell.groupWindows ? pinnedWindows : null
                groupSlot: pinnedSlot.modelData.appId
                groupWindowApp: ""
                width: shell.iconsOnly ? Theme.barButtonWidth : 160
                Layout.preferredWidth: width; Layout.preferredHeight: height
                DragHandler {
                    target: null
                    yAxis.enabled: false
                    onActiveTranslationChanged: if (active) pinnedSlot.drag(this)
                    onActiveChanged: if (!active) pinnedSlot.drop()
                }
            }
        }
    }
}
