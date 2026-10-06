// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The window switcher: every window's icon and title in a grid, most recently focused first,
// with the selected one's full title and place below. A click picks a window.
Rectangle {
    id: switcher
    required property size screenSize
    // The compositor's switcher, unless set (as a preview sets them).
    property var windows: shell.switcherWindows
    property int selected: shell.switcherSelected
    readonly property int cell: 132
    readonly property int padding: 16
    // As many columns as fit in most of the output's width, and rows up to most of its height;
    // the grid scrolls to the selection past that.
    readonly property int columns: Math.max(1, Math.min(windows.length, Math.floor((screenSize.width * 0.9 - 2 * padding) / cell)))
    readonly property int rows: Math.max(1, Math.min(Math.ceil(windows.length / columns), Math.floor((screenSize.height * 0.8 - 2 * padding - 48) / cell)))
    readonly property var current: windows[selected] || ({})
    width: columns * cell + 2 * padding
    height: rows * cell + 2 * padding + caption.height + 8
    radius: Theme.radiusLarge
    color: Theme.surface
    border.color: Theme.border

    GridView {
        id: grid
        objectName: "switcherGrid"
        x: switcher.padding; y: switcher.padding
        width: switcher.columns * switcher.cell; height: switcher.rows * switcher.cell
        cellWidth: switcher.cell; cellHeight: switcher.cell
        interactive: false
        clip: true
        model: switcher.windows
        currentIndex: switcher.selected
        highlightMoveDuration: 0
        highlight: Rectangle {
            radius: Theme.radiusMedium
            color: Theme.accentSubtle
            border.color: Theme.accent; border.width: 2
        }
        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            width: switcher.cell; height: switcher.cell
            opacity: modelData.minimized ? 0.6 : 1
            Image {
                id: icon
                anchors.horizontalCenter: parent.horizontalCenter
                y: 18; width: 56; height: 56; sourceSize: Qt.size(56, 56)
                source: "image://icons/" + shell.iconFor(entry.modelData.appId)
            }
            Text {
                anchors.top: icon.bottom; anchors.topMargin: 8
                anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                text: entry.modelData.title.length > 0 ? entry.modelData.title : entry.modelData.appId
                textFormat: Text.PlainText
                color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            // A window asking for attention has a dot in the urgent colour over its icon.
            Rectangle {
                objectName: "switcherUrgent"
                visible: entry.modelData.urgent === true
                anchors.right: icon.right; anchors.top: icon.top
                width: 12; height: 12; radius: 6
                color: Theme.urgent; border.width: 2; border.color: Theme.surface
            }
            MouseArea { anchors.fill: parent; onClicked: shell.switcherPick(entry.index) }
        }
    }
    Column {
        id: caption
        anchors.top: grid.bottom; anchors.topMargin: 8
        x: switcher.padding; width: switcher.width - 2 * switcher.padding
        Text {
            width: parent.width
            text: switcher.current.title || switcher.current.appId || ""
            textFormat: Text.PlainText
            color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.bold: true
            horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
        }
        Text {
            width: parent.width
            text: switcher.current.output ? "Workspace " + switcher.current.workspace + " on " + switcher.current.output + (switcher.current.minimized ? " · minimized" : "") + (switcher.current.urgent ? " · needs attention" : "") : ""
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
            horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
        }
    }
}
