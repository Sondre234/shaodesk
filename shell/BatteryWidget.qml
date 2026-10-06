// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The battery: an outline filled to the charge, red when nearly empty and not charging,
// in the accent colour while charging.
FlatButton {
    id: battery
    required property var panel
    required property real barHeight
    objectName: "batteryWidget"
    readonly property var status: battery.panel.statusSource
    readonly property bool low: status.batteryPercent <= 15 && status.batteryState !== "charging"
    readonly property color tint: low ? Theme.danger : (status.batteryState === "charging" ? Theme.accent : Theme.text)
    visible: shell.widgets.battery && status.batteryPresent
    Layout.preferredWidth: 62; Layout.preferredHeight: battery.barHeight - 10
    Accessible.name: status.batteryText
    BarTip { panel: battery.panel; owner: battery; text: battery.status.batteryText }
    contentItem: Row {
        spacing: 5
        anchors.centerIn: parent
        Item {
            anchors.verticalCenter: parent.verticalCenter; width: 24; height: 12
            Rectangle {
                width: 21; height: 12; radius: 2; color: "transparent"
                border.color: battery.tint; border.width: 1
                Rectangle {
                    objectName: "batteryLevel"
                    x: 2; y: 2; height: parent.height - 4; radius: 1
                    width: Math.max(1, (parent.width - 4) * battery.status.batteryPercent / 100)
                    color: battery.tint
                }
            }
            Rectangle { x: 21; y: 4; width: 2; height: 4; color: battery.tint }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: battery.status.batteryPercent + "%"
            color: battery.tint
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
