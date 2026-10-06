// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import Shaodesk

ListView {
    id: taskList
    required property var panel
    required property real barHeight
    objectName: "taskList"
    // As tall as a button, so its tasks line up with the pinned ones: a horizontal
    // list places each delegate at its top, whatever the delegate's own y.
    Layout.fillWidth: true; Layout.preferredHeight: shell.panelHeight - 10
    orientation: ListView.Horizontal; spacing: 4; clip: true
    // Dragging moves a single task, not the list; the wheel scrolls an overflowing one.
    interactive: false
    // The task being dragged, the task whose place it takes, and how far the tasks
    // in between step aside.
    property int dragFrom: -1
    property int dragTo: -1
    property real dragStep: 0
    model: TaskFilter { controller: shell; sourceModel: taskList.panel.taskSource; grouped: shell.groupWindows }
    moveDisplaced: Transition { NumberAnimation { property: "x"; duration: Theme.durationFast; easing.type: Theme.easing } }
    WheelHandler {
        enabled: taskList.contentWidth > taskList.width
        onWheel: (event) => {
            var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
            taskList.contentX = Math.max(0, Math.min(taskList.contentWidth - taskList.width,
                                                     taskList.contentX - delta / 2))
        }
    }
    delegate: TaskButton {
        id: taskButton
        required property int index
        panel: taskList.panel
        // Windows without an app id have nothing to group by.
        property TaskFilter appWindows: TaskFilter { controller: shell; sourceModel: taskList.panel.taskSource; windowApp: taskButton.appId }
        group: shell.groupWindows && appId !== "" ? appWindows : null
        width: shell.iconsOnly ? 40 : Math.min(185, Math.max(92, taskList.width / Math.max(1, taskList.count) - 4))
        z: reorder.active ? 1 : 0
        readonly property real shift: {
            var from = taskList.dragFrom, to = taskList.dragTo
            if (from < 0 || index === from)
                return 0
            if (from < to && index > from && index <= to)
                return -taskList.dragStep
            if (from > to && index >= to && index < from)
                return taskList.dragStep
            return 0
        }
        property real shiftX: shift
        Behavior on shiftX { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        transform: Translate { x: reorder.active ? reorder.activeTranslation.x : taskButton.shiftX }
        // The task follows the pointer through the list, the tasks it passes halfway
        // over making way, and moves on release; the press never becomes a click.
        DragHandler {
            id: reorder
            target: null
            yAxis.enabled: false
            onActiveTranslationChanged: {
                if (!active)
                    return
                // Tasks are all as wide, so past a neighbour's middle is half a step.
                var step = taskButton.width + taskList.spacing
                var to = taskButton.index + Math.round(activeTranslation.x / step)
                taskList.dragStep = step
                taskList.dragTo = Math.max(0, Math.min(taskList.count - 1, to))
                taskList.dragFrom = taskButton.index
            }
            onActiveChanged: {
                if (active)
                    return
                var from = taskList.dragFrom, to = taskList.dragTo
                taskList.dragFrom = taskList.dragTo = -1
                // The filter reports a move as a new layout, which rebuilds every
                // task, this one and its drag handler with them.
                // By then this task's context is gone, taking the list's id with it.
                var list = taskList
                if (from >= 0 && to >= 0 && to !== from)
                    Qt.callLater(function() {
                        var contentX = list.contentX
                        list.model.move(from, to, 1)
                        list.contentX = contentX
                    })
            }
        }
    }
}
