// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A small button of the start menu's headings, as "All apps ›" and "‹ Back": its text with a
// chevron after it, or before it with `back` set, on a raised fill.
AbstractButton {
    id: button
    property bool back: false
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    leftPadding: back ? Theme.spacingM : Theme.spacingL
    rightPadding: back ? Theme.spacingL : Theme.spacingM
    topPadding: Theme.spacingS; bottomPadding: Theme.spacingS
    Accessible.name: text
    background: Rectangle {
        radius: Theme.radiusSmall
        color: button.pressed ? Theme.mix(Theme.surfaceRaisedHover, Theme.text, 0.05)
             : button.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
        border.color: Theme.border
    }
    contentItem: Row {
        spacing: Theme.spacingS
        layoutDirection: button.back ? Qt.RightToLeft : Qt.LeftToRight
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: button.back ? "chevron-left" : "chevron-right"
            size: Theme.iconSizeSmall
            color: Theme.textMuted
        }
    }
}
