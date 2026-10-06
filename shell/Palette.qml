// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// The command palette: a search box over windows, applications, workspaces, actions and saved
// sessions. Up and Down (or Ctrl+N and Ctrl+P) select, Enter runs, Escape closes; a leading
// > @ # or % narrows the search to actions, windows, workspaces or sessions. It is a card with
// room around it for its shadow, coming in each time its window shows.
Item {
    id: root
    required property size screenSize
    readonly property int padding: Theme.spacingL
    // A result's row: a title over a subtitle.
    readonly property int rowHeight: Theme.rowHeight + Theme.spacingL
    readonly property int visibleRows: Math.max(1, Math.min(8, Math.floor((screenSize.height * 0.6 - 3 * padding - input.height) / rowHeight)))
    readonly property var kindLabels: ({ window: "Window", app: "App", workspace: "Workspace", action: "Action", session: "Session" })
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the card's opacity, and what is left of its drop and
    // growth. It goes quicker than it came, what it ran showing through.
    property bool shown: false
    property real progress: 0
    states: State {
        name: "shown"
        when: root.shown
        PropertyChanges { root.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]

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
        height: input.height + (list.count > 0 ? list.height : empty.height) + 3 * root.padding
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

        // The start menu's field, larger.
        SearchInput {
            id: input
            objectName: "paletteInput"
            x: root.padding; y: root.padding
            width: card.width - 2 * root.padding
            large: true
            placeholderText: "Search windows, apps, workspaces, actions, sessions"
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
            // The selection is shaded as a menu's row is, it being what Enter runs; it moves at
            // once, as fast as the keys go.
            highlightMoveDuration: 0
            highlight: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.selected
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            // A result: its icon, its title over what it is, and its kind on a pill at the end.
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width; height: root.rowHeight
                Image {
                    id: icon
                    x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
                    width: Theme.appIconSizeLarge; height: Theme.appIconSizeLarge
                    sourceSize: Qt.size(2 * Theme.appIconSizeLarge, 2 * Theme.appIconSizeLarge)
                    source: "image://icons/" + row.modelData.icon
                }
                // A window asking for attention has a dot in the urgent colour on its icon, as
                // in the switcher.
                Rectangle {
                    visible: row.modelData.urgent === true
                    x: icon.x + icon.width - width + Theme.spacingXS; y: icon.y - Theme.spacingXS
                    width: Theme.spacingL; height: width; radius: width / 2
                    color: Theme.urgent; border.width: 2; border.color: Theme.surface
                }
                Column {
                    anchors.left: icon.right; anchors.leftMargin: Theme.spacingL
                    anchors.right: kind.left; anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingXS
                    Text {
                        width: parent.width
                        text: row.modelData.title; textFormat: Text.PlainText
                        color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.subtitle; textFormat: Text.PlainText
                        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                Rectangle {
                    id: kind
                    anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    width: kindLabel.implicitWidth + 2 * Theme.spacingM
                    height: kindLabel.implicitHeight + 2 * Theme.spacingXS
                    radius: height / 2
                    color: "transparent"
                    border.color: Theme.border
                    Text {
                        id: kindLabel
                        anchors.centerIn: parent
                        text: root.kindLabels[row.modelData.kind] || ""
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.Medium
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: shell.palette.selected = row.index
                    onClicked: shell.palette.activate(row.index)
                }
            }
        }
        // What to try when nothing matches: the prefixes that narrow a search.
        Column {
            id: empty
            objectName: "paletteEmpty"
            visible: list.count === 0
            x: root.padding; y: input.y + input.height + root.padding
            width: card.width - 2 * root.padding
            topPadding: Theme.spacingL; bottomPadding: Theme.spacingL
            spacing: Theme.spacingS
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "search"; size: Theme.iconSizeLarge; color: Theme.textMuted
            }
            Text {
                width: parent.width
                text: "Nothing matches"
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                text: "Start with > for actions, @ for windows, # for workspaces or % for sessions"
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
            }
        }
    }
}
