// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The bell in the panel: opens the notification history, shows how many notifications have not
// been seen, and turns into a crossed-out bell while do-not-disturb is on. Right-click toggles
// do-not-disturb.
Button {
    id: bell
    required property var panel
    required property real barHeight
    objectName: "notificationBell"
    readonly property var center: shell.notifications
    readonly property string description: (center.dnd ? "Do not disturb, " : "") +
        (center.unread > 0 ? center.unread + " unread notifications" : "No new notifications")
    visible: center.serving
    Layout.preferredWidth: 40; Layout.preferredHeight: barHeight - 10
    hoverEnabled: true
    onClicked: panel.toggleAudioPopup("notifications", bell)
    Accessible.name: description
    ToolTip.visible: hovered && !pressed && !panel.menuOpen
    ToolTip.delay: 500
    ToolTip.text: description + "\nRight-click: do not disturb"
    Component.onCompleted: if ("popupType" in ToolTip.toolTip) ToolTip.toolTip.popupType = Popup.Window
    background: Rectangle {
        radius: 7
        color: panel.audioPopup === "notifications" ? Qt.lighter(shell.panelColor, 1.8) : (bell.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: bell.center.toggleDnd()
    }
    // The compositor's notification_history action opens it on the output under the pointer.
    Connections {
        target: shell
        function onNotificationsRequested(output) {
            if (output === panel.outputName) panel.toggleAudioPopup("notifications", bell)
        }
    }
    contentItem: Item {
        Icon {
            id: glyph
            objectName: "notificationGlyph"
            anchors.centerIn: parent
            name: bell.center.dnd ? "bell-off" : "bell"
            color: bell.center.dnd ? "#8a96a8" : shell.textColor
        }
        Rectangle {
            objectName: "notificationBadge"
            visible: bell.center.unread > 0
            x: parent.width / 2 + 2; y: parent.height / 2 - 16
            width: Math.max(15, badgeText.implicitWidth + 8); height: 15; radius: 7.5
            color: bell.center.dnd ? "#627084" : "#e8564b"
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: bell.center.unread > 9 ? "9+" : bell.center.unread
                color: "#ffffff"; font.pixelSize: 10; font.bold: true
            }
        }
    }
}
