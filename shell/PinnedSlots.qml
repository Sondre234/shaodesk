// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Shaodesk

// A pinned application's slot shows its launcher, or its windows while it has any.
// Dragging either slides the slot along the bar, the slots it passes halfway over
// making way; dropping it keeps it where it was dragged to.
Repeater {
    id: pinnedSlots
    required property var panel
    required property real barHeight
    model: shell.pinned
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
        // The transform's own x, not the item's: it does not fight the layout.
        // qmllint disable Quick.layout-positioning
        transform: Translate { x: pinnedSlot.dragging ? pinnedSlot.dragX : pinnedSlot.shiftX }
        // qmllint enable Quick.layout-positioning
        Button {
            id: pinnedButton
            objectName: "pinned:" + pinnedSlot.modelData.appId
            visible: pinnedTasks.count === 0
            Layout.preferredWidth: 40; Layout.preferredHeight: pinnedSlots.barHeight - 10
            // Padded like a window's button, so the icon stays put when one opens.
            topPadding: 2; bottomPadding: 6
            onClicked: { if (shell.launch(pinnedSlot.modelData.appId)) pinnedSlots.panel.closeMenus() }
            Accessible.name: pinnedSlot.modelData.name
            background: Rectangle { radius: 7; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
            contentItem: Item {
                Image {
                    readonly property int size: Math.min(22, parent.height)
                    anchors.centerIn: parent; width: size; height: size
                    source: "image://icons/" + pinnedSlot.modelData.icon; sourceSize: Qt.size(size, size)
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
            delegate: TaskButton {
                objectName: "pinnedTask:" + pinnedSlot.modelData.appId
                panel: pinnedSlots.panel
                iconName: pinnedSlot.modelData.icon
                group: shell.groupWindows ? pinnedWindows : null
                groupSlot: pinnedSlot.modelData.appId
                groupWindowApp: ""
                width: shell.iconsOnly ? 40 : 160
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
