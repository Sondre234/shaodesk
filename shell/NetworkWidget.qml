// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The network: ascending bars for Wi-Fi, a plug for a wired link, dimmed and struck through
// when the interface is down.
Button {
    id: network
    required property var panel
    required property real barHeight
    objectName: "networkWidget"
    readonly property var status: network.panel.statusSource
    readonly property bool linkDown: status.networkState === "disconnected"
    readonly property color tint: down ? "#8a96a8" : shell.textColor
    visible: shell.widgets.network && status.networkState !== "none"
    Layout.preferredWidth: 34; Layout.preferredHeight: network.barHeight - 10
    hoverEnabled: true
    Accessible.name: status.networkText
    background: Rectangle { radius: 7; color: network.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
    BarTip { panel: network.panel; owner: network; text: network.status.networkText }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: network.status.networkState === "ethernet" ? "ethernet-port" : (network.linkDown ? "wifi-off" : "wifi")
            color: network.tint
        }
    }
}
