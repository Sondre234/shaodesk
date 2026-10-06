// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The Quick Settings button, left of the clock as on Windows 11: the status of the widgets
// shell.widgets puts in Quick Settings ("quick") in small icons (the network, the volume, the
// battery with its charge), there while any widget is placed there. Clicking opens the flyout;
// the wheel changes the volume and a middle click mutes it, as on the volume control.
FlatButton {
    id: button
    required property var panel
    required property real barHeight
    objectName: "quickSettingsButton"
    readonly property var widgets: shell.widgets
    readonly property var audio: panel.audioSource
    readonly property var status: panel.statusSource
    readonly property bool showNetwork: widgets.network === "quick" && status.networkState !== "none"
    // The sound server the panel follows, which a test may take away as it ends.
    readonly property bool showVolume: widgets.volume === "quick" && !!audio && audio.available
    readonly property bool showBattery: widgets.battery === "quick" && status.batteryPresent
    readonly property string description: {
        var lines = []
        if (showNetwork) lines.push(status.networkText)
        if (showVolume) lines.push("Volume " + audio.volume + "%" + (audio.muted ? ", muted" : ""))
        if (showBattery) lines.push(status.batteryText)
        return lines.length > 0 ? lines.join("\n") : "Quick settings"
    }
    visible: ["network", "battery", "volume", "tiling", "profiles", "wallpapers", "notifications"]
        .some(function(name) { return button.widgets[name] === "quick" })
    Layout.preferredWidth: icons.implicitWidth + 2 * Theme.spacingM
    Layout.preferredHeight: barHeight - 10
    hoverEnabled: true
    active: panel.audioPopup === "quick"
    onClicked: panel.toggleAudioPopup("quick", button)
    Accessible.name: "Quick settings: " + description
    BarTip { panel: button.panel; owner: button; text: button.description }
    contentItem: Item {
        Row {
            id: icons
            anchors.centerIn: parent
            spacing: Theme.spacingM
            FadingIcon {
                visible: button.showNetwork
                anchors.verticalCenter: parent.verticalCenter
                name: button.status.networkState === "ethernet" ? "ethernet-port"
                    : button.status.networkState === "disconnected" ? "wifi-off" : "wifi"
                color: button.status.networkState === "disconnected" ? Theme.textMuted : Theme.text
            }
            SpeakerIcon {
                visible: button.showVolume
                anchors.verticalCenter: parent.verticalCenter
                level: button.showVolume ? button.audio.volume : 0
                muted: button.showVolume && button.audio.muted
                color: muted ? Theme.textMuted : Theme.text
            }
            BatteryIcon {
                visible: button.showBattery
                anchors.verticalCenter: parent.verticalCenter
                status: button.status
            }
            // With none of those to show, a sign that settings are behind it.
            Icon {
                visible: !button.showNetwork && !button.showVolume && !button.showBattery
                anchors.verticalCenter: parent.verticalCenter
                name: "sliders-horizontal"
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        onClicked: if (button.showVolume) button.audio.toggleMute()
    }
    // Five percent a wheel notch, up for louder.
    WheelHandler {
        property real travel: 0
        enabled: button.showVolume
        onWheel: (event) => {
            travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
            var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
            travel -= steps * 120
            if (steps !== 0)
                button.audio.changeVolume(steps * 5)
        }
    }
}
