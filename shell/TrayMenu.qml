// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A tray item's menu, in the style of the bar's own: a submenu's entries take the place of the
// menu's, with a way back, as the bar menu's appearance entry does. A long one scrolls.
Rectangle {
    id: trayMenu
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "trayMenu"
    readonly property int rowHeight: 34
    readonly property var entries: panel.trayEntries(panel.trayMenuKey, panel.trayMenuParent,
                                                    panel.trayMenuTrail, panel.trayMenuRevision)
    readonly property real widest: {
        var widest = 0
        for (var i = 0; i < entries.length; ++i)
            widest = Math.max(widest, menuFont.advanceWidth(entries[i].label))
        return widest
    }
    readonly property real listHeight: {
        var sum = 0
        for (var i = 0; i < entries.length; ++i)
            sum += entries[i].separator ? 9 : rowHeight
        return Math.max(rowHeight, sum)
    }
    FontMetrics { id: menuFont; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
    visible: panel.trayMenuKey !== ""
    width: Math.min(panel.width - 16, Math.max(180, widest + 80))
    height: Math.min(panel.popupLayer.height - shell.panelExtent - 20, 12 + listHeight)
    x: Math.max(8, Math.min(panel.trayMenuX - width / 2, panel.width - width - 8))
    y: panel.onTop ? panel.barBottom + 8 : panel.barTop - height - 8
    color: Theme.surface; radius: Theme.radiusMedium
    border.color: Theme.border
    MouseArea { anchors.fill: parent }
    ListView {
        id: trayEntryList
        anchors.fill: parent; anchors.margins: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: trayMenu.entries
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Button {
            id: trayEntry
            required property var modelData
            objectName: modelData.separator ? "trayMenuSeparator" : "trayMenuItem"
            width: ListView.view.width
            height: modelData.separator ? 9 : trayMenu.rowHeight
            text: modelData.label
            enabled: !modelData.separator && modelData.enabled
            leftPadding: 4; rightPadding: 6
            Accessible.name: modelData.label + (modelData.toggle !== "" ? (modelData.checked ? ", checked" : ", not checked") : "")
            onClicked: panel.trayMenuPick(modelData)
            background: Rectangle {
                radius: Theme.radiusSmall
                color: trayEntry.hovered && trayEntry.enabled ? Theme.hover : "transparent"
                Rectangle {
                    visible: trayEntry.modelData.separator
                    anchors.verticalCenter: parent.verticalCenter
                    x: 8; width: parent.width - 16; height: 1
                    color: Theme.divider
                }
            }
            contentItem: RowLayout {
                visible: !trayEntry.modelData.separator
                spacing: 8
                opacity: trayEntry.enabled ? 1 : 0.4
                // A check box, a radio button's dot, or the entry's icon.
                Item {
                    Layout.preferredWidth: 16; Layout.preferredHeight: 16
                    Rectangle {
                        visible: trayEntry.modelData.toggle === "radio"
                        anchors.centerIn: parent
                        width: 10; height: 10; radius: 5
                        color: trayEntry.modelData.checked ? Theme.accent : "transparent"
                        border.color: trayEntry.modelData.checked ? Theme.accent : Theme.textMuted
                    }
                    Rectangle {
                        visible: trayEntry.modelData.toggle === "checkmark"
                        anchors.centerIn: parent
                        width: 13; height: 13; radius: 3
                        color: trayEntry.modelData.checked ? Theme.accent : "transparent"
                        border.color: trayEntry.modelData.checked ? Theme.accent : Theme.textMuted
                        Text {
                            visible: trayEntry.modelData.checked
                            anchors.centerIn: parent
                            text: "\u2713"; color: Theme.textOnAccent; font.pixelSize: 10; font.bold: true
                        }
                    }
                    Image {
                        visible: trayEntry.modelData.toggle === "" && trayEntry.modelData.icon !== ""
                        anchors.centerIn: parent
                        width: 16; height: 16
                        source: visible ? trayEntry.modelData.icon : ""
                        sourceSize: Qt.size(16, 16)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: trayEntry.modelData.label; textFormat: Text.PlainText; elide: Text.ElideRight
                    color: Theme.text; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                }
                Text {
                    visible: trayEntry.modelData.submenu
                    text: "\u203a"; color: Theme.text; font.pixelSize: Theme.fontSize + 4
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: trayMenu.entries.length === 0
            text: "No entries"
            color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
