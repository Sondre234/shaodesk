// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Shaodesk

// The taskbar (shell.style "taskbar"): the bar along the panel's edge, as Windows 11 has it, with
// the start button, the pinned applications and the windows, then the workspaces, the widgets
// shell.widgets puts on the bar, Quick Settings, the clock and the show-desktop sliver. It fills
// the panel's surface and draws the bar inset by shell.panel_margin.
Item {
    id: taskbar
    required property var panel
    // What the panel reaches: the bar itself, which the popups open by, and the buttons that open
    // them, as MenuBar.qml names its own.
    readonly property Item barItem: bar
    readonly property Item tasks: taskList
    readonly property Item pins: pinnedSlots
    readonly property Item trayRow: tray
    readonly property Item clock: clockButton
    readonly property Item volume: audioWidget
    readonly property Item profiles: profilesButton
    readonly property Item wallpapers: wallpapersButton
    readonly property Item quickSettings: quickButton
    // Inset from the output's edges or rounded, the bar floats.
    readonly property bool floating: shell.panelRadius > 0 || shell.panelMarginLeft > 0 ||
                                     shell.panelMarginRight > 0 || shell.panelMarginTop > 0 ||
                                     shell.panelMarginBottom > 0

    Rectangle {
        id: bar
        objectName: "bar"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: shell.panelMarginLeft; anchors.rightMargin: shell.panelMarginRight
        anchors.bottom: taskbar.panel.onTop ? undefined : parent.bottom
        anchors.top: taskbar.panel.onTop ? parent.top : undefined
        anchors.bottomMargin: shell.panelMarginBottom; anchors.topMargin: shell.panelMarginTop
        height: shell.panelHeight
        color: Theme.bar
        radius: shell.panelRadius
        // A floating bar gets an outline; a docked one a line along its inner edge.
        border.width: taskbar.floating ? 1 : 0
        border.color: Theme.border
        Rectangle {
            visible: !taskbar.floating
            y: taskbar.panel.onTop ? parent.height - 1 : 0
            width: parent.width; height: 1; color: Theme.border
        }
        // Right-clicking the bar anywhere but on a task opens the bar's own menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: (mouse) => taskbar.panel.openContextMenu(bar, mouse.x, -1)
        }
        // Scrolling the bar anywhere its widgets leave the wheel alone pages through this
        // output's workspaces: a wheel notch (or a touchpad's worth of travel) moves one,
        // stopping at either end; down or right goes to the next.
        WheelHandler {
            property real travel: 0
            onWheel: (event) => {
                travel += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                travel -= steps * 120
                if (steps !== 0) {
                    var target = workspaceIndicator.workspaceState.current - steps
                    workspaceIndicator.show(Math.max(1, Math.min(shell.workspaceCount, target)))
                }
            }
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: Theme.spacingM; anchors.rightMargin: Theme.spacingM
            spacing: Theme.spacingS
            FlatButton {
                id: start
                Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
                active: taskbar.panel.launcherOpen
                onClicked: taskbar.panel.launcherOpen = !taskbar.panel.launcherOpen
                Accessible.name: "Applications"
                BarTip { panel: taskbar.panel; owner: start; text: "Start" }
                // Four squares in the accent colour, which shrink a little while pressed as an
                // application's icon does.
                contentItem: Item {
                    Grid {
                        anchors.centerIn: parent; columns: 2; spacing: 3
                        scale: start.pressed ? Theme.pressScale : 1
                        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        Repeater { model: 4; Rectangle { width: 9; height: 9; radius: 2; color: Theme.accent } }
                    }
                }
            }
            PinnedSlots { id: pinnedSlots; panel: taskbar.panel }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: Theme.barButtonHeight / 2 + Theme.spacingS; color: Theme.divider }
            TaskList { id: taskList; panel: taskbar.panel }
            WorkspaceIndicator { id: workspaceIndicator; panel: taskbar.panel }
            // The wallpaper picker.
            FlatButton {
                id: wallpapersButton
                objectName: "wallpapersButton"
                visible: shell.widgets.wallpapers === "bar"
                Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
                active: taskbar.panel.audioPopup === "wallpapers"
                onClicked: taskbar.panel.toggleAudioPopup("wallpapers", wallpapersButton)
                Accessible.name: "Wallpapers"
                BarTip { panel: taskbar.panel; owner: wallpapersButton; text: "Wallpapers" }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "image"; color: wallpapersButton.active ? Theme.accent : Theme.text }
                }
            }
            // The appearance profile in use; clicking lists the profiles to switch to.
            FlatButton {
                id: profilesButton
                objectName: "profilesButton"
                visible: shell.widgets.profiles === "bar" && shell.profiles.length > 1
                Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
                active: taskbar.panel.audioPopup === "profiles"
                onClicked: taskbar.panel.toggleAudioPopup("profiles", profilesButton)
                Accessible.name: "Appearance: " + (shell.profile || "none")
                BarTip { panel: taskbar.panel; owner: profilesButton; text: "Appearance: " + (shell.profile || "none") }
                // Three swatches of the profile in use: accent, desktop background, text.
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: Theme.spacingXS
                        Repeater {
                            model: [shell.accent, shell.background, shell.textColor]
                            // A ring in the text colour keeps a swatch close to the panel's own
                            // colour visible.
                            Rectangle {
                                required property color modelData
                                width: 10; height: 10; radius: 5
                                color: modelData
                                border.width: 1
                                border.color: Theme.alpha(Theme.text, 0.5)
                            }
                        }
                    }
                }
            }
            FlatButton {
                id: tilingToggle
                objectName: "tilingToggle"
                visible: shell.widgets.tiling === "bar"
                Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
                enabled: shell.tilingAvailable
                opacity: enabled ? 1 : 0.4
                active: taskbar.panel.tiling
                onClicked: { taskbar.panel.closeMenus(); shell.toggleTiling(taskbar.panel.outputName) }
                Accessible.name: taskbar.panel.tiling ? "Tiling on" : "Tiling off"
                BarTip { panel: taskbar.panel; owner: tilingToggle; text: taskbar.panel.tiling ? "Tiling on: click for floating" : "Floating: click to tile" }
                // On: a split layout in the accent colour. Off: two overlapping windows.
                contentItem: Item {
                    FadingIcon {
                        objectName: "tilingIcon"
                        anchors.centerIn: parent
                        name: taskbar.panel.tiling ? "layout-panel-left" : "copy"
                        color: taskbar.panel.tiling ? Theme.accent : Theme.text
                    }
                }
            }
            // The system tray: the status icons of applications, in the order they appeared.
            // Passive ones stay hidden, and so does the tray when none is left.
            Row {
                id: tray
                objectName: "tray"
                visible: shell.widgets.tray && shell.tray.shown > 0
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    model: shell.tray
                    delegate: TrayButton { panel: taskbar.panel }
                }
            }
            NotificationBell { id: bell; panel: taskbar.panel }
            NetworkWidget { panel: taskbar.panel }
            BatteryWidget { panel: taskbar.panel }
            VolumeButton { id: audioWidget; panel: taskbar.panel }
            KeyboardLayout { panel: taskbar.panel }
            QuickSettingsButton { id: quickButton; panel: taskbar.panel }
            ClockButton { id: clockButton; panel: taskbar.panel }
            // A sliver at the bar's end, its line taking the accent colour under the pointer.
            Button {
                id: showDesktopButton
                Layout.preferredWidth: Theme.spacingL + Theme.spacingXS; Layout.fillHeight: true
                onClicked: { taskbar.panel.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                BarTip { panel: taskbar.panel; owner: showDesktopButton; text: "Show desktop" }
                background: Rectangle {
                    anchors.right: parent.right
                    width: 3
                    color: showDesktopButton.pressed ? Theme.accentHover : showDesktopButton.hovered ? Theme.accent : Theme.border
                    Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                }
            }
        }
        Rectangle {
            visible: shell.error.length > 0
            anchors.fill: parent; anchors.margins: Theme.spacingS
            color: Theme.dangerSurface; radius: Theme.radiusSmall
            Text { anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: Theme.spacingL; text: shell.error; color: Theme.text; elide: Text.ElideRight; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
            CloseButton {
                id: dismiss
                anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: "Dismiss"
                onClicked: shell.clearError()
            }
        }
    }
}
