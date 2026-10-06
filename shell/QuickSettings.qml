// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Quick Settings flyout, as on Windows 11, at the bar's right end: tiles for what
// shell.widgets puts in it ("quick") and for night light, the volume with the outputs and the
// applications' volumes a click away, the screen's brightness where it has a backlight, and the
// battery along its foot. What sits on the bar instead keeps its own button there.
PopupCard {
    id: quick
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "quickSettings"
    open: panel.audioPopup === "quick"
    readonly property var widgets: shell.widgets
    readonly property var status: panel.statusSource
    readonly property var audio: panel.audioSource
    readonly property var backlight: panel.backlightSource
    readonly property var center: shell.notifications
    // Which list is open under its tile or row: "profiles", "outputs", "mixer", or "" for none.
    property string expanded: ""
    function toggle(list) { expanded = expanded === list ? "" : list }
    onOpened: expanded = ""
    readonly property real padding: Theme.spacingXL
    // As wide as the clock's flyout, which it lines up with.
    implicitWidth: 7 * (Theme.rowHeight + Theme.spacingL) + 2 * padding
    implicitHeight: content.implicitHeight + 2 * padding + (footer.visible ? footer.height : 0)
    anchorRect: panel.barAnchor(barItem.x + barItem.width, 0)
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    readonly property string outputName: {
        var outputs = audio.outputs
        for (var i = 0; i < outputs.length; ++i)
            if (outputs[i].name === audio.output) return outputs[i].description
        return "No output"
    }

    // A row's chevron that opens or closes a list under it.
    component Expander: FlatButton {
        id: expander
        property bool expanded: false
        Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
        active: expanded
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: "chevron-right"; size: Theme.iconSizeSmall; color: Theme.textMuted
                rotation: expander.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
        }
    }

    // What does not fit scrolls, above the foot.
    Flickable {
        anchors.fill: parent
        anchors.bottomMargin: footer.visible ? footer.height : 0
        contentHeight: content.implicitHeight + 2 * quick.padding
        clip: contentHeight > height
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: content
            x: quick.padding; y: quick.padding
            width: quick.width - 2 * quick.padding
            spacing: Theme.spacingL
            GridLayout {
                id: tiles
                Layout.fillWidth: true
                columns: 3
                columnSpacing: Theme.spacingM; rowSpacing: Theme.spacingL
                QuickTile {
                    objectName: "quickTile:dnd"
                    visible: quick.widgets.notifications === "quick" && quick.center.serving
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: checked ? "bell-off" : "bell"
                    label: "Do not disturb"
                    checked: quick.center.dnd
                    onClicked: quick.center.toggleDnd()
                }
                QuickTile {
                    objectName: "quickTile:nightLight"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "moon"
                    label: "Night light"
                    detail: shell.nightLightMode === "auto" ? "Scheduled" : ""
                    checked: shell.nightLight
                    onClicked: shell.send("night_light_toggle")
                }
                QuickTile {
                    objectName: "quickTile:tiling"
                    visible: quick.widgets.tiling === "quick"
                    interactive: shell.tilingAvailable
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "layout-panel-left"
                    label: "Tiling"
                    detail: "This monitor"
                    checked: quick.panel.tiling
                    onClicked: shell.toggleTiling(quick.panel.outputName)
                }
                QuickTile {
                    objectName: "quickTile:profiles"
                    visible: quick.widgets.profiles === "quick" && shell.profiles.length > 1
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "palette"
                    label: "Appearance"
                    detail: shell.profile
                    expandable: true
                    expanded: quick.expanded === "profiles"
                    onClicked: quick.toggle("profiles")
                }
                QuickTile {
                    objectName: "quickTile:wallpapers"
                    visible: quick.widgets.wallpapers === "quick"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "image"
                    label: "Wallpaper"
                    // The bar's own picker, by the Quick Settings button.
                    onClicked: quick.panel.toggleAudioPopup("wallpapers", quick.panel.quickSettingsButton)
                }
                QuickTile {
                    objectName: "quickTile:network"
                    visible: quick.widgets.network === "quick" && quick.status.networkState !== "none"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    // Only the state: shaodesk does not manage connections.
                    status: true
                    glyph: quick.status.networkState === "ethernet" ? "ethernet-port"
                        : quick.status.networkState === "wifi" ? "wifi" : "wifi-off"
                    label: quick.status.networkState === "ethernet" ? "Ethernet"
                         : quick.status.networkState === "wifi" ? "Wi-Fi" : "Disconnected"
                    detail: quick.status.networkInterface
                    checked: quick.status.networkState === "ethernet" || quick.status.networkState === "wifi"
                }
            }
            // The appearance profiles, under their tile.
            Column {
                objectName: "quickProfiles"
                visible: quick.expanded === "profiles"
                Layout.fillWidth: true
                Repeater {
                    model: shell.profiles.map(function(name) {
                        return { text: name, toggle: "radio", checked: name === shell.profile }
                    })
                    MenuRow {
                        objectName: "quickProfileItem"
                        width: parent.width
                        iconColumn: true
                        onClicked: if (modelData.text !== shell.profile) shell.pickProfile(modelData.text)
                    }
                }
            }
            // The screen's brightness, where it has a backlight.
            RowLayout {
                objectName: "quickBrightness"
                visible: quick.backlight.present
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Item {
                    Layout.preferredWidth: Theme.rowHeight - Theme.spacingS; Layout.preferredHeight: Theme.rowHeight - Theme.spacingS
                    Icon { anchors.centerIn: parent; name: "sun" }
                }
                AudioSlider {
                    objectName: "quickBrightnessSlider"
                    Layout.fillWidth: true
                    value: Math.max(0, quick.backlight.percent)
                    Accessible.name: "Brightness"
                    onMoved: quick.backlight.setPercent(Math.round(value))
                }
                Text {
                    Layout.preferredWidth: Theme.rowHeight
                    // Level with the volume's percentage, whose row has a chevron after it.
                    Layout.rightMargin: Theme.rowHeight + Theme.spacingS
                    text: quick.backlight.percent + "%"; horizontalAlignment: Text.AlignRight
                    color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
            }
            // The default output's volume, the outputs to play through and each application's
            // volume.
            ColumnLayout {
                objectName: "quickSound"
                visible: quick.widgets.volume === "quick" && quick.audio.available
                Layout.fillWidth: true
                spacing: Theme.spacingXS
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    MuteButton {
                        objectName: "quickMute"
                        level: quick.audio.volume; muted: quick.audio.muted
                        Accessible.name: muted ? "Unmute" : "Mute"
                        onClicked: quick.audio.toggleMute()
                    }
                    AudioSlider {
                        objectName: "quickVolumeSlider"
                        Layout.fillWidth: true
                        value: quick.audio.volume; muted: quick.audio.muted
                        Accessible.name: "Volume"
                        onMoved: quick.audio.setVolume(Math.round(value))
                    }
                    Text {
                        Layout.preferredWidth: Theme.rowHeight
                        text: quick.audio.volume + "%"; horizontalAlignment: Text.AlignRight
                        color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                    Expander {
                        objectName: "quickOutputsToggle"
                        expanded: quick.expanded === "outputs"
                        Accessible.name: "Output: " + quick.outputName
                        onClicked: quick.toggle("outputs")
                    }
                }
                // The outputs, the one in use marked.
                Column {
                    objectName: "quickOutputs"
                    visible: quick.expanded === "outputs"
                    Layout.fillWidth: true
                    Repeater {
                        model: [{ header: "Output" }].concat(quick.audio.outputs.map(function(output) {
                            return { text: output.description, name: output.name, toggle: "radio",
                                     checked: output.name === quick.audio.output }
                        }))
                        MenuRow {
                            objectName: modelData.header ? "quickOutputsHeading" : "quickOutputItem"
                            width: parent.width
                            iconColumn: true
                            onClicked: quick.audio.setOutput(modelData.name)
                        }
                    }
                }
                // The applications playing sound, and their volumes under it.
                FlatButton {
                    id: mixerToggle
                    objectName: "quickMixerToggle"
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.rowHeight
                    leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                    active: quick.expanded === "mixer"
                    onClicked: quick.toggle("mixer")
                    Accessible.name: "Applications"
                    contentItem: RowLayout {
                        spacing: Theme.spacingS
                        Text {
                            Layout.fillWidth: true
                            text: "Applications"; elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        }
                        Text {
                            text: quick.audio.streams.count > 0 ? quick.audio.streams.count : "None playing"
                            color: Theme.textMuted; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                        }
                        Icon {
                            name: "chevron-right"; size: Theme.iconSizeSmall; color: Theme.textMuted
                            rotation: mixerToggle.active ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        }
                    }
                }
                Column {
                    objectName: "quickStreams"
                    visible: quick.expanded === "mixer"
                    Layout.fillWidth: true
                    Text {
                        visible: quick.audio.streams.count === 0
                        width: parent.width; height: Theme.rowHeight
                        text: "No applications are playing sound"
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                    Repeater {
                        model: quick.audio.streams
                        RowLayout {
                            id: streamRow
                            required property int streamId
                            required property string name
                            required property string icon
                            required property int volume
                            required property bool muted
                            width: parent.width; height: Theme.rowHeight + Theme.spacingM
                            spacing: Theme.spacingS
                            Image {
                                source: "image://icons/" + streamRow.icon
                                sourceSize: Qt.size(2 * Theme.appIconSize, 2 * Theme.appIconSize)
                                Layout.preferredWidth: Theme.appIconSize; Layout.preferredHeight: Theme.appIconSize
                                Layout.leftMargin: Theme.spacingXS; Layout.rightMargin: Theme.spacingXS
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    text: streamRow.name; textFormat: Text.PlainText; elide: Text.ElideRight
                                    color: Theme.text; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                                }
                                AudioSlider {
                                    objectName: "quickStreamSlider"
                                    Layout.fillWidth: true; Layout.preferredHeight: Theme.iconSize + Theme.spacingS
                                    value: streamRow.volume; muted: streamRow.muted
                                    Accessible.name: streamRow.name
                                    onMoved: quick.audio.setStreamVolume(streamRow.streamId, Math.round(value))
                                }
                            }
                            MuteButton {
                                level: streamRow.volume; muted: streamRow.muted
                                Accessible.name: (muted ? "Unmute " : "Mute ") + streamRow.name
                                onClicked: quick.audio.toggleStreamMute(streamRow.streamId)
                            }
                        }
                    }
                }
            }
        }
    }
    // The battery, along the foot of the card.
    Rectangle {
        id: footer
        objectName: "quickBattery"
        visible: quick.widgets.battery === "quick" && quick.status.batteryPresent
        // Inside the card's outline.
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 1
        height: Theme.rowHeight + Theme.spacingL
        bottomLeftRadius: quick.radius - 1; bottomRightRadius: quick.radius - 1
        color: Theme.surfaceRaised
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Row {
            anchors.left: parent.left; anchors.leftMargin: quick.padding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingM
            BatteryIcon { anchors.verticalCenter: parent.verticalCenter; status: quick.status }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: quick.status.batteryPercent + "%" +
                      (quick.status.batteryState === "charging" ? " · Charging"
                       : quick.status.batteryState === "full" ? " · Full" : "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
    }
}
