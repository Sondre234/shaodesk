// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Quick Settings flyout, as on Windows 11, at the bar's right end: tiles for what
// shell.widgets puts in it ("quick") and for night light, and the battery along its foot. What
// sits on the bar instead keeps its own button there.
PopupCard {
    id: quick
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "quickSettings"
    open: panel.audioPopup === "quick"
    readonly property var widgets: shell.widgets
    readonly property var status: panel.statusSource
    readonly property var center: shell.notifications
    // Which list is open under its tile: "profiles", or "" for none.
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
                    interactive: false
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
