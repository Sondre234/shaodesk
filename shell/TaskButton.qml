// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Shaodesk

// A window on the taskbar: its icon, with its title beside it unless shell.iconsOnly, else
// as a tooltip. A pinned slot shows its launcher's icon rather than the window's.
// With shell.groupWindows the button stands for all its application's windows, `group`:
// stacked when there are several, it cycles through them and lists them on hover.
Button {
    id: task
    required property int taskId
    required property string title
    required property string appId
    required property bool active
    required property bool minimized
    required property bool urgent
    // The panel it is on, whose menus and group list it opens.
    required property Item panel
    // A window names its own app ID; a path in it is no icon to load from disk.
    property string iconName: appId.indexOf("/") >= 0 ? "application-x-executable" : appId
    property TaskFilter group: null
    // Where the hover list finds the group's windows (see the panel's groupSlot).
    property string groupSlot: ""
    property string groupWindowApp: appId
    readonly property int windows: group ? group.count : 1
    readonly property bool stacked: windows > 1
    readonly property bool shownActive: group && group.count > 0 ? group.activeTask >= 0 : active
    readonly property bool shownMinimized: group && group.count > 0 ? group.minimized : minimized
    // Asking for attention: any window of a stack will do.
    readonly property bool shownUrgent: group && group.count > 0 ? group.urgent : urgent
    height: shell.panelHeight - 10
    // Hovering a stacked button lists its windows, whatever the platform thinks of hover.
    hoverEnabled: true
    // The icon sits above the activity line, fitting however short the bar is.
    topPadding: 2; bottomPadding: 6; leftPadding: 6; rightPadding: 6
    onClicked: {
        task.panel.closeMenus()
        shell.tasks.activate(stacked ? group.nextTask() : taskId)
    }
    onHoveredChanged: task.panel.hoverGroup(task, hovered)
    Accessible.name: (stacked ? title + " and " + (windows - 1) + " more" : title) + (shownUrgent ? " (needs attention)" : "")
    // The panel's surface is only as tall as the bar, so an in-window tooltip would be
    // squeezed onto the icon and swallow its clicks; a popup window of its own sits above
    // the bar instead.
    BarTip {
        panel: task.panel; owner: task
        visible: shell.iconsOnly && !task.stacked && task.hovered && !task.panel.expanded && !task.pressed
        text: task.title
    }
    // How far the activity line has come in, from 0 to 1, for a button that arrives (its
    // window opening) to draw it out from its middle.
    property real reveal: 1
    NumberAnimation on reveal {
        id: revealing
        running: false
        from: 0; to: 1
        duration: Theme.durationNormal; easing.type: Theme.easing
    }
    function appear() { revealing.restart() }
    background: ButtonFill {
        hovered: task.hovered
        pressed: task.pressed
        active: task.shownActive
        color: task.shownUrgent ? Theme.urgentSubtle : stateColor
        border.width: task.shownUrgent ? 1 : 0; border.color: Theme.urgent
        // Several windows: a second button's edge peeks out behind this one.
        Rectangle {
            visible: task.stacked
            z: -1; x: 3; y: 2; width: parent.width; height: parent.height - 4; radius: Theme.radiusSmall
            color: "transparent"; border.width: 1
            border.color: task.shownActive ? Theme.alpha(Theme.text, 0.3) : Theme.border
        }
        // The activity line along the bottom: long under the focused window's button, short
        // under the others, split in two for a stack; in the accent colour, the urgent one, or
        // dimmed while minimized. Its length and colour ease to each new state.
        Item {
            id: line
            objectName: "taskLine"
            readonly property real segment: (task.shownActive ? (shell.iconsOnly ? 18 : 28) / (task.stacked ? 2 : 1)
                                                              : (task.stacked ? 6 : 10)) * task.reveal
            property real first: segment
            property real second: task.stacked ? segment : 0
            property real gap: task.stacked ? 3 : 0
            property color tint: task.shownUrgent ? Theme.urgent : task.shownMinimized ? Theme.textDisabled : Theme.accent
            Behavior on first { enabled: !revealing.running; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on second { enabled: !revealing.running; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on gap { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on tint { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
            width: first + gap + second; height: 3
            Rectangle { width: line.first; height: parent.height; radius: 1; color: line.tint }
            Rectangle {
                visible: width > 0
                x: line.first + line.gap; width: line.second; height: parent.height; radius: 1
                color: line.tint
            }
        }
        // A dot that pulses a few times when the window asks for attention, then holds.
        Rectangle {
            objectName: "taskUrgent"
            visible: task.shownUrgent
            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 2
            width: 8; height: 8; radius: 4
            color: Theme.urgent
            SequentialAnimation on opacity {
                running: task.shownUrgent
                loops: 6; alwaysRunToEnd: true
                NumberAnimation { to: 0.3; duration: Theme.duration(450) }
                NumberAnimation { to: 1; duration: Theme.duration(450) }
            }
        }
        Rectangle {
            objectName: "taskCount"
            visible: task.stacked
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 1
            width: Math.max(14, count.implicitWidth + 6); height: 14; radius: 7
            color: Theme.accent
            Text { id: count; anchors.centerIn: parent; text: task.windows; color: Theme.textOnAccent; font.pixelSize: 10; font.bold: true; font.family: Theme.fontFamily }
        }
    }
    contentItem: RowLayout {
        spacing: 6
        Item { Layout.fillWidth: shell.iconsOnly }
        Image {
            readonly property int size: Math.min(Theme.appIconSize, task.availableHeight)
            source: "image://icons/" + task.iconName; sourceSize: Qt.size(size, size)
            Layout.preferredWidth: size; Layout.preferredHeight: size
        }
        Text { visible: !shell.iconsOnly; text: task.title; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
        Item { Layout.fillWidth: shell.iconsOnly }
    }
    // Right-click opens the task's menu; middle-click closes its window.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                task.panel.openContextMenu(task, 0, task.taskId, task.appId)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton) { task.panel.closeMenus(); shell.tasks.close(task.taskId) }
        }
    }
}
