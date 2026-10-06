// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A tooltip for something on the bar, in a popup window of its own: the panel's surface is only
// as tall as the bar. Hidden while a popup is open.
ToolTip {
    id: barTip
    required property Item owner
    required property var panel
    visible: owner.hovered && !owner.pressed && text.length > 0 && !panel.menuOpen
    delay: 500
    y: panel.onTop ? owner.height + 6 : -implicitHeight - 6
    Component.onCompleted: if ("popupType" in barTip) barTip.popupType = Popup.Window
}
