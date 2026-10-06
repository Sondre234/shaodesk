// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Shaodesk

// The windows of a hovered stacked button: clicking one focuses it (or minimizes it when
// focused already), the cross or a middle click closes it, and a right click opens its menu.
Rectangle {
    id: groupList
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "groupList"
    readonly property int rowHeight: 40
    visible: panel.groupOpen
    width: 280; height: 12 + groupWindows.count * rowHeight + Math.max(0, groupWindows.count - 1) * 2
    x: Math.max(8, Math.min(panel.groupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 10
    border.color: Qt.lighter(shell.panelColor, 1.6)
    HoverHandler {
        id: groupHover
        onHoveredChanged: groupList.panel.hoverGroupList(hovered)
    }
    readonly property bool hovered: groupHover.hovered
    TaskFilter {
        id: groupWindows
        controller: shell; sourceModel: panel.taskSource
        app: panel.groupSlot; windowApp: panel.groupWindowApp
        // A window closing may leave nothing to choose between.
        onCountChanged: if (count < 2) panel.groupOpen = false
    }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 2
        Repeater {
            model: panel.groupOpen ? groupWindows : null
            delegate: Button {
                id: groupWindow
                required property int taskId
                required property string title
                required property string appId
                required property bool active
                required property bool minimized
                required property bool urgent
                objectName: "groupWindow"
                width: parent.width; height: groupList.rowHeight
                Accessible.name: title
                // Closing the list destroys this row, so it goes last.
                onClicked: { shell.tasks.activate(taskId); panel.groupOpen = false }
                background: Rectangle {
                    radius: 6
                    color: groupWindow.hovered ? Qt.lighter(shell.panelColor, 1.5) : (groupWindow.active ? Qt.lighter(shell.panelColor, 1.3) : "transparent")
                    Rectangle { visible: groupWindow.active; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1; color: shell.accent }
                    Rectangle { objectName: "groupWindowUrgent"; visible: groupWindow.urgent; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1; color: shell.urgentColor }
                }
                contentItem: RowLayout {
                    spacing: 8
                    Image {
                        Layout.leftMargin: 4
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        source: "image://icons/" + panel.groupIcon; sourceSize: Qt.size(20, 20)
                        opacity: groupWindow.minimized ? 0.5 : 1
                    }
                    Text {
                        Layout.fillWidth: true
                        text: groupWindow.title; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: groupWindow.urgent ? shell.urgentColor : groupWindow.minimized ? Qt.darker(shell.textColor, 1.4) : shell.textColor
                        font.pixelSize: shell.fontSize; font.family: panel.uiFont
                    }
                    Button {
                        id: closeWindow
                        objectName: "groupWindowClose"
                        visible: groupWindow.hovered || hovered
                        Layout.preferredWidth: 24; Layout.preferredHeight: 24
                        Accessible.name: "Close " + groupWindow.title
                        onClicked: shell.tasks.close(groupWindow.taskId)
                        background: Rectangle { radius: 5; color: closeWindow.hovered ? "#c4443c" : "transparent" }
                        contentItem: Text { text: "\u2715"; color: shell.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            panel.openContextMenu(groupWindow, 0, groupWindow.taskId, groupWindow.appId)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) shell.tasks.close(groupWindow.taskId)
                    }
                }
            }
        }
    }
}
