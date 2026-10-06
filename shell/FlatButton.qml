// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A button with no frame of its own, as on the bar and in menus: its background shows only
// while it is hovered or pressed, and while it is `active` (what it opens is open, what it
// toggles is on, the row the keyboard is at).
Button {
    id: button
    property bool active: false
    property real radius: Theme.radiusSmall
    hoverEnabled: true
    background: Rectangle {
        radius: button.radius
        color: button.pressed ? Theme.pressed
               : button.active ? Theme.selected
               : button.hovered ? Theme.hover : "transparent"
    }
}
