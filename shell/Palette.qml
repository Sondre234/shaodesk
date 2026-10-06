// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

// The command palette: a search box over windows, applications, workspaces, actions and saved
// sessions. Up and Down (or Ctrl+N and Ctrl+P) select, Enter runs, Escape closes; a leading
// > @ # or % narrows the search to actions, windows, workspaces or sessions. It is a card with
// room around it for its shadow, coming in each time its window shows.
Item {
    id: root
    required property size screenSize
    readonly property int padding: 12
    readonly property int rowHeight: 48
    readonly property int visibleRows: Math.max(1, Math.min(8, Math.floor((screenSize.height * 0.6 - 3 * padding - input.height) / rowHeight)))
    readonly property var kindLabels: ({ window: "Window", app: "App", workspace: "Workspace", action: "Action", session: "Session" })
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // How far it has come in, from 0 to 1, each time its window shows: the card's opacity, and
    // what is left of its drop and growth. It goes at once, for what it runs to show.
    property real progress: 0
    states: State {
        name: "shown"
        when: root.Window.window !== null && root.Window.window.visible
        PropertyChanges { root.progress: 1 }
    }
    transitions: Transition {
        to: "shown"
        NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
    }

    function reset() {
        input.text = shell.palette.query
        input.forceActiveFocus()
    }
    Connections {
        target: shell.palette
        function onQueryChanged() { if (input.text !== shell.palette.query) input.text = shell.palette.query }
    }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        width: Math.min(680, root.screenSize.width - 32)
        height: input.height + (list.count > 0 ? list.height + root.padding : 0) + 2 * root.padding
        opacity: root.progress
        scale: 0.97 + 0.03 * root.progress
        transform: Translate { y: (root.progress - 1) * Theme.spacingM }
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.radiusLarge
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.surface
            border.color: Theme.border
        }

        TextField {
            id: input
            objectName: "paletteInput"
            x: root.padding; y: root.padding
            width: card.width - 2 * root.padding
            height: 40
            color: Theme.text
            placeholderText: "Search windows, apps, workspaces, actions, sessions"
            placeholderTextColor: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle
            selectByMouse: true
            background: Rectangle {
                radius: Theme.radiusSmall; color: Theme.surfaceRaised
                border.color: input.activeFocus ? Theme.accent : "transparent"
            }
            onTextChanged: shell.palette.query = text
            Keys.onPressed: function(event) {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab || (ctrl && (event.key === Qt.Key_N || event.key === Qt.Key_J))) {
                    shell.palette.move(1); event.accepted = true
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Backtab || (ctrl && (event.key === Qt.Key_P || event.key === Qt.Key_K))) {
                    shell.palette.move(-1); event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    shell.palette.move(root.visibleRows); event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    shell.palette.move(-root.visibleRows); event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    shell.palette.activate(); event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    shell.palette.close(); event.accepted = true
                }
            }
        }

        ListView {
            id: list
            objectName: "paletteList"
            x: root.padding; y: input.y + input.height + root.padding
            width: card.width - 2 * root.padding
            height: Math.min(count, root.visibleRows) * root.rowHeight
            clip: true
            interactive: false
            model: shell.palette.results
            currentIndex: shell.palette.selected
            highlightMoveDuration: 0
            highlight: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.accentSubtle
                border.color: Theme.accent
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width; height: root.rowHeight
                Image {
                    id: icon
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28; sourceSize: Qt.size(28, 28)
                    source: "image://icons/" + row.modelData.icon
                }
                Column {
                    anchors.left: icon.right; anchors.leftMargin: 12
                    anchors.right: kind.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: row.modelData.title; textFormat: Text.PlainText
                        color: row.modelData.urgent === true ? Theme.urgent : Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.subtitle; textFormat: Text.PlainText
                        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                Text {
                    id: kind
                    anchors.right: parent.right; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.kindLabels[row.modelData.kind] || ""
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: shell.palette.selected = row.index
                    onClicked: shell.palette.activate(row.index)
                }
            }
        }
    }
}
