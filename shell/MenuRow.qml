// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One row of a PopupMenu, drawn from its entry (`modelData`, see PopupMenu.qml): a separator, a
// section header, or an entry with its check mark, radio dot or icon, its label, its secondary
// text and, for a submenu, a chevron. `highlighted` is the row the pointer or the keyboard is at,
// `expanded` the one whose submenu is open; a disabled row is greyed out and cannot be chosen, and
// a destructive one (`danger`) is in the danger colour.
AbstractButton {
    id: row
    required property var modelData
    required property int index
    property bool highlighted: false
    property bool expanded: false
    // Whether the menu's rows keep a column for icons and marks, so that labels line up.
    property bool iconColumn: false
    property real rowHeight: 36
    property real labelStart: Theme.spacingL
    property real labelEnd: Theme.spacingM

    readonly property bool separator: modelData.separator === true
    readonly property bool header: !separator && (modelData.header || "") !== ""
    readonly property bool submenu: !!modelData.submenu
    readonly property string toggle: modelData.toggle === "checkmark" ? "check" : (modelData.toggle || "")
    readonly property bool marked: modelData.checked === true
    readonly property bool danger: modelData.danger === true
    readonly property color ink: !enabled ? Theme.textDisabled : danger ? Theme.danger : Theme.text

    text: header ? modelData.header : (modelData.text || "")
    enabled: !separator && !header && modelData.enabled !== false
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: separator ? 9 : header ? 30 : rowHeight
    Accessible.role: separator ? Accessible.Separator : Accessible.MenuItem
    Accessible.name: text
    Accessible.checkable: toggle !== ""
    Accessible.checked: marked

    background: Rectangle {
        radius: Theme.radiusSmall
        color: !row.enabled ? "transparent"
               : row.pressed ? Theme.pressed
               : row.highlighted || row.expanded ? Theme.hover : "transparent"
        Rectangle {
            visible: row.separator
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingS; width: parent.width - 2 * Theme.spacingS; height: 1
            color: Theme.divider
        }
    }
    contentItem: RowLayout {
        visible: !row.separator
        spacing: Theme.spacingL
        // A check mark, a radio button's dot, or the entry's icon: a line icon Icon knows, else
        // a theme icon's name or an image's URL.
        Item {
            visible: row.iconColumn && !row.header
            Layout.leftMargin: row.labelStart
            Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
            Icon {
                visible: row.toggle === "check" && row.marked
                anchors.centerIn: parent
                name: "check"; size: Theme.iconSizeSmall; color: Theme.accent
            }
            Rectangle {
                visible: row.toggle === "radio" && row.marked
                anchors.centerIn: parent
                width: 8; height: 8; radius: 4
                color: Theme.accent
            }
            Icon {
                id: glyph
                visible: row.toggle === "" && known
                anchors.centerIn: parent
                name: row.toggle === "" ? (row.modelData.icon || "") : ""
                size: Theme.iconSizeSmall; color: row.ink
            }
            Image {
                readonly property string icon: row.toggle === "" && !glyph.known ? (row.modelData.icon || "") : ""
                visible: icon !== ""
                anchors.fill: parent
                opacity: row.enabled ? 1 : 0.4
                source: icon === "" ? "" : icon.indexOf(":") >= 0 || icon.charAt(0) === "/" ? icon
                                                                                          : "image://icons/" + icon
                sourceSize: Qt.size(Theme.iconSizeSmall, Theme.iconSizeSmall)
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: row.iconColumn && !row.header ? 0 : row.labelStart
            text: row.text; textFormat: Text.PlainText; elide: Text.ElideRight
            color: row.header ? Theme.textMuted : row.ink
            font.pixelSize: row.header ? Theme.fontSizeSmall : Theme.fontSize
            font.family: Theme.fontFamily
        }
        Text {
            visible: text !== "" && !row.header
            Layout.rightMargin: row.submenu ? 0 : row.labelEnd
            text: row.modelData.secondary || ""; textFormat: Text.PlainText
            color: row.enabled ? Theme.textMuted : Theme.textDisabled
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Icon {
            visible: row.submenu && !row.header
            Layout.rightMargin: row.labelEnd
            name: "chevron-right"; size: Theme.iconSizeSmall
            color: row.enabled ? Theme.textMuted : Theme.textDisabled
        }
    }
}
