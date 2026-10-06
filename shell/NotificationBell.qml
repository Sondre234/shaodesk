// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The bell in the panel (shell.widgets.notifications = "bar"; the clock does the same anyway):
// opens the clock flyout with the notifications, shows how many have not been seen, and turns into
// a crossed-out bell while do-not-disturb is on. Right-click toggles do-not-disturb.
FlatButton {
    id: bell
    required property var panel
    required property real barHeight
    objectName: "notificationBell"
    readonly property var center: shell.notifications
    readonly property string description: (center.dnd ? "Do not disturb, " : "") +
        (center.unread > 0 ? center.unread + " unread notifications" : "No new notifications")
    visible: shell.widgets.notifications === "bar" && center.serving
    Layout.preferredWidth: 40; Layout.preferredHeight: barHeight - 10
    hoverEnabled: true
    onClicked: panel.toggleAudioPopup("clock", bell)
    Accessible.name: description
    BarTip { panel: bell.panel; owner: bell; text: bell.description + "\nRight-click: do not disturb" }
    active: panel.audioPopup === "clock"
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: bell.center.toggleDnd()
    }
    contentItem: Item {
        Icon {
            id: glyph
            objectName: "notificationGlyph"
            anchors.centerIn: parent
            name: bell.center.dnd ? "bell-off" : "bell"
            color: bell.center.dnd ? Theme.textMuted : Theme.text
        }
        Rectangle {
            objectName: "notificationBadge"
            visible: bell.center.unread > 0
            x: parent.width / 2 + 2; y: parent.height / 2 - 16
            width: Math.max(15, badgeText.implicitWidth + 8); height: 15; radius: 7.5
            color: bell.center.dnd ? Theme.textMuted : Theme.dangerFill
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: bell.center.unread > 9 ? "9+" : bell.center.unread
                color: bell.center.dnd ? Theme.surface : Theme.textOnDanger; font.pixelSize: 10; font.bold: true
            }
        }
    }
}
