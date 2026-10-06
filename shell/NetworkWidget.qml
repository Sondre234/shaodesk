// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The network: ascending bars for Wi-Fi, a plug for a wired link, dimmed and struck through
// when the interface is down.
FlatButton {
    id: network
    required property var panel
    objectName: "networkWidget"
    readonly property var status: network.panel.statusSource
    readonly property bool linkDown: status.networkState === "disconnected"
    readonly property color tint: linkDown ? Theme.textMuted : Theme.text
    visible: shell.widgets.network === "bar" && status.networkState !== "none"
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    Accessible.name: status.networkText
    BarTip { panel: network.panel; owner: network; text: network.status.networkText }
    contentItem: Item {
        FadingIcon {
            anchors.centerIn: parent
            name: network.status.networkState === "ethernet" ? "ethernet-port" : (network.linkDown ? "wifi-off" : "wifi")
            color: network.tint
        }
    }
}
