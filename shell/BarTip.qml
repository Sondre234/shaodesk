// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A tooltip for something on the bar, in a popup window of its own: the panel's surface is only
// as tall as the bar. Hidden while a popup is open. Its text is shown as it is, never read as
// markup: it is often a window's title or an application's.
ToolTip {
    id: barTip
    required property Item owner
    required property var panel
    visible: owner.hovered && !owner.pressed && text.length > 0 && !panel.menuOpen
    delay: 500
    width: Math.min(implicitWidth, 420)
    y: panel.onTop ? owner.height + 6 : -implicitHeight - 6
    contentItem: Text {
        text: barTip.text; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: Theme.text; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
    }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radiusSmall }
    Component.onCompleted: if ("popupType" in barTip) barTip.popupType = Popup.Window
}
