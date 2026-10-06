// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A search field, as the start menu's and the command palette's: a rounded field on the raised
// surface with a magnifier before the text, outlined in the accent colour while it has the
// keyboard. `large` is the palette's, the one thing on its card, in the title's type.
TextField {
    id: field
    property bool large: false
    readonly property real iconSize: large ? Theme.iconSize : Theme.iconSizeSmall
    implicitHeight: Theme.rowHeight + Theme.spacingS + (large ? Theme.spacingXS : 0)
    leftPadding: Theme.spacingL + iconSize + Theme.spacingM
    rightPadding: Theme.spacingL
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: large ? Theme.fontSizeTitle : Theme.fontSize
    font.family: Theme.fontFamily
    background: Rectangle {
        radius: height / 2
        color: Theme.surfaceRaised
        border.color: field.activeFocus ? Theme.accent : Theme.border
        Icon {
            x: Theme.spacingL; anchors.verticalCenter: parent.verticalCenter
            name: "search"; size: field.iconSize
            color: field.activeFocus ? Theme.text : Theme.textMuted
        }
    }
}
