// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A small button of the start menu, as "All apps ›" and "‹ Back" above its views and those of
// the best match of a search: its text on a raised fill, an icon before it (a line icon Icon.qml
// draws, else a theme icon's name or an image's path), and a chevron after it, or before it with
// `back` set. `primary` fills it with the accent colour, for what a click mostly does.
AbstractButton {
    id: button
    property bool back: false
    property bool chevron: true
    property bool primary: false
    property string iconName
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    leftPadding: back || iconName !== "" ? Theme.spacingM : Theme.spacingL
    rightPadding: chevron && !back ? Theme.spacingM : Theme.spacingL
    topPadding: Theme.spacingS; bottomPadding: Theme.spacingS
    Accessible.name: text
    background: Rectangle {
        radius: Theme.radiusSmall
        color: button.primary ? (button.pressed ? Theme.mix(Theme.accent, Theme.textOnAccent, 0.25)
                                 : button.hovered ? Theme.accentHover : Theme.accent)
             : button.pressed ? Theme.mix(Theme.surfaceRaisedHover, Theme.text, 0.05)
             : button.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
        border.color: button.primary ? "transparent" : Theme.border
    }
    contentItem: Row {
        spacing: Theme.spacingS
        layoutDirection: button.back ? Qt.RightToLeft : Qt.LeftToRight
        Item {
            visible: button.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.iconSizeSmall; height: Theme.iconSizeSmall
            Icon {
                id: glyph
                visible: known
                name: button.iconName
                size: Theme.iconSizeSmall
                color: button.primary ? Theme.textOnAccent : Theme.text
            }
            Image {
                visible: !glyph.known && button.iconName !== ""
                anchors.fill: parent
                sourceSize: Qt.size(width, height)
                source: !visible ? "" : button.iconName.charAt(0) === "/" ? button.iconName
                                                                         : "image://icons/" + button.iconName
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            color: button.primary ? Theme.textOnAccent : Theme.text
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Icon {
            visible: button.chevron
            anchors.verticalCenter: parent.verticalCenter
            name: button.back ? "chevron-left" : "chevron-right"
            size: Theme.iconSizeSmall
            color: Theme.textMuted
        }
    }
}
