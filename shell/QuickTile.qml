// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A tile of the Quick Settings flyout, as on Windows 11: a button with a line icon (`glyph`),
// filled with the accent colour while what it toggles is `checked`, and its label under it. A
// tile that opens a list (`expandable`) shows a chevron, turned while `expanded`; one that cannot
// be used now (`interactive` false, as tiling where it cannot be) is greyed out. A tile that only
// tells something (`status`, as the network's) is no button at all: its icon sits on a disc in
// the face's place, tinted with the accent colour while `checked` (connected), and it takes no
// clicks.
AbstractButton {
    id: tile
    property string glyph
    property string label
    // A second line under the label: the value in use, or more about the state.
    property string detail
    property bool expandable: false
    property bool expanded: false
    property bool interactive: true
    property bool status: false
    checkable: false
    enabled: interactive && !status
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: status ? Accessible.StaticText : Accessible.Button
    Accessible.name: label + (detail ? ", " + detail : "")
    Accessible.checkable: !expandable && !status
    Accessible.checked: checked
    implicitHeight: face.height + Theme.spacingS + caption.implicitHeight
    // Tiles in a row line up by their buttons, whether or not they have a second line.
    Layout.alignment: Qt.AlignTop
    readonly property bool on: checked && !status
    readonly property bool greyed: !interactive && !status
    // The button, or for a status the place it would be.
    Rectangle {
        id: face
        width: parent.width; height: Theme.rowHeight + Theme.spacingL
        radius: Theme.radiusMedium
        color: tile.status ? "transparent"
             : tile.on ? (tile.pressed ? Theme.mix(Theme.accent, Theme.text, 0.2)
                                       : tile.hovered ? Theme.accentHover : Theme.accent)
             : tile.pressed || (tile.hovered && tile.enabled) ? Theme.surfaceRaisedHover : Theme.surfaceRaised
        border.color: tile.on || tile.status ? "transparent" : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        Rectangle {
            visible: tile.status
            anchors.centerIn: parent
            width: parent.height - Theme.spacingM; height: width; radius: width / 2
            color: tile.checked ? Theme.accentSubtle : Theme.selected
        }
        Icon {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: tile.expandable ? -Theme.spacingS : 0
            name: tile.glyph; size: Theme.iconSize
            color: tile.on ? Theme.textOnAccent : tile.greyed ? Theme.textDisabled
                 : tile.status ? (tile.checked ? Theme.accent : Theme.textMuted) : Theme.text
        }
        Icon {
            visible: tile.expandable
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"; size: Theme.iconSizeSmall; rotation: tile.expanded ? 90 : 0
            color: tile.on ? Theme.textOnAccent : Theme.textMuted
            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
    }
    Column {
        id: caption
        y: face.height + Theme.spacingS
        width: parent.width
        Text {
            width: parent.width
            text: tile.label; textFormat: Text.PlainText; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            color: tile.greyed ? Theme.textDisabled : Theme.text
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Text {
            visible: text.length > 0
            width: parent.width
            text: tile.detail; textFormat: Text.PlainText; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
        }
    }
    background: Item {}
    contentItem: Item {}
}
