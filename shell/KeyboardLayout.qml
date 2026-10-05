// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The active keyboard layout's short name ("us", "no"), shown while the keymap has more than
// one. Clicking it switches every keyboard to the next.
Button {
    id: indicator
    required property var panel
    required property real barHeight
    objectName: "keyboardLayout"
    readonly property var layout: shell.keyboardLayout
    readonly property string label: layout.short || ""
    readonly property string description: "Keyboard layout: " + (layout.name || label)
    visible: shell.widgets.keyboard_layout && (layout.count || 0) > 1
    Layout.preferredWidth: Math.max(40, text.implicitWidth + 16); Layout.preferredHeight: barHeight - 10
    hoverEnabled: true
    onClicked: { panel.closeMenus(); shell.send("switch_layout next") }
    Accessible.name: description
    ToolTip.visible: hovered && !pressed && !panel.menuOpen
    ToolTip.delay: 500
    ToolTip.text: description + "\nClick: next layout"
    Component.onCompleted: if ("popupType" in ToolTip.toolTip) ToolTip.toolTip.popupType = Popup.Window
    background: Rectangle {
        radius: 7
        color: indicator.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent"
    }
    contentItem: Text {
        id: text
        objectName: "keyboardLayoutText"
        text: indicator.label
        color: shell.textColor
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        font.pixelSize: shell.fontSize; font.weight: Font.DemiBold; font.family: indicator.panel.uiFont
    }
}
