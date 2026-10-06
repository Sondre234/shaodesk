// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// The window switcher: every window's icon and title in a grid, most recently focused first,
// with the selected one's full title and place below, on a card with room around it for its
// shadow. A click picks a window.
Item {
    id: switcher
    required property size screenSize
    // The compositor's switcher, unless set (as a preview sets them).
    property var windows: shell.switcherWindows
    property int selected: shell.switcherSelected
    // What it lists, held as it was while it goes: the compositor forgets the windows as the
    // switcher closes.
    property var listed: []
    property int listedSelected: 0
    Binding on listed { when: switcher.shown; value: switcher.windows; restoreMode: Binding.RestoreNone }
    Binding on listedSelected { when: switcher.shown; value: switcher.selected; restoreMode: Binding.RestoreNone }
    // A window's cell: its icon over two lines of its title.
    readonly property int cell: Theme.appIconSizeDisplay + 10 * Theme.spacingM
    readonly property int padding: Theme.spacingXL
    // As many columns as fit in most of the output's width, and rows up to most of its height;
    // the grid scrolls to the selection past that.
    readonly property int columns: Math.max(1, Math.min(listed.length, Math.floor((screenSize.width * 0.9 - 2 * padding) / cell)))
    readonly property int rows: Math.max(1, Math.min(Math.ceil(listed.length / columns), Math.floor((screenSize.height * 0.8 - 2 * padding - 48) / cell)))
    readonly property var current: listed[listedSelected] || ({})
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the card's opacity and growth. It comes in on the theme's
    // normal duration and goes on its fast one, quicker than it came, as the switch it ends is
    // done.
    property bool shown: false
    property real progress: 0
    states: State {
        name: "shown"
        when: switcher.shown
        PropertyChanges { switcher.progress: 1 }
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

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        opacity: switcher.progress
        scale: 0.94 + 0.06 * switcher.progress
        width: switcher.columns * switcher.cell + 2 * switcher.padding
        height: switcher.rows * switcher.cell + 2 * switcher.padding + caption.height + caption.anchors.topMargin
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
        GridView {
            id: grid
            objectName: "switcherGrid"
            x: switcher.padding; y: switcher.padding
            width: switcher.columns * switcher.cell; height: switcher.rows * switcher.cell
            cellWidth: switcher.cell; cellHeight: switcher.cell
            interactive: false
            clip: true
            model: switcher.listed
            currentIndex: switcher.listedSelected
            // The selection glides from window to window, once the switcher has come in.
            highlightMoveDuration: switcher.progress >= 1 ? Theme.durationNormal : 0
            highlight: Item {
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.spacingXS
                    radius: Theme.radiusMedium
                    color: Theme.accentSubtle
                    border.color: Theme.accent; border.width: 2
                }
            }
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                width: switcher.cell; height: switcher.cell
                readonly property bool minimized: modelData.minimized === true
                readonly property bool urgent: modelData.urgent === true
                // A window asking for attention is tinted in the urgent colour, but for the
                // selection's own.
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.spacingXS
                    radius: Theme.radiusMedium
                    color: entry.urgent && !entry.GridView.isCurrentItem ? Theme.urgentSubtle : "transparent"
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: pick.containsMouse ? Theme.hover : "transparent"
                    }
                }
                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.spacingXL
                    width: Theme.appIconSizeDisplay; height: Theme.appIconSizeDisplay
                    sourceSize: Qt.size(2 * Theme.appIconSizeDisplay, 2 * Theme.appIconSizeDisplay)
                    source: "image://icons/" + shell.iconFor(entry.modelData.appId)
                    // A minimized window's is faded.
                    opacity: entry.minimized ? 0.45 : 1
                }
                Text {
                    anchors.top: icon.bottom; anchors.topMargin: Theme.spacingM + Theme.spacingXS
                    anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.spacingM
                    text: entry.modelData.title.length > 0 ? entry.modelData.title : entry.modelData.appId
                    textFormat: Text.PlainText
                    color: entry.minimized ? Theme.textMuted : Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                }
                // Badges on the icon's corners: a dot in the urgent colour for a window asking for
                // attention, a dash for a minimized one.
                Rectangle {
                    objectName: "switcherUrgent"
                    visible: entry.urgent
                    x: icon.x + icon.width - width + Theme.spacingXS; y: icon.y - Theme.spacingXS
                    width: Theme.iconSizeSmall; height: width; radius: width / 2
                    color: Theme.urgent; border.width: 2; border.color: Theme.surface
                }
                Rectangle {
                    objectName: "switcherMinimized"
                    visible: entry.minimized
                    x: icon.x + icon.width - width + Theme.spacingXS
                    y: icon.y + icon.height - height + Theme.spacingXS
                    width: Theme.iconSize + Theme.spacingS; height: width; radius: width / 2
                    color: Theme.surfaceRaised; border.color: Theme.border
                    Icon {
                        anchors.centerIn: parent
                        name: "minus"; size: Theme.iconSizeSmall; color: Theme.textMuted
                    }
                }
                MouseArea {
                    id: pick
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shell.switcherPick(entry.index)
                }
            }
        }
        // A label on a small pill, for what the caption says of a window's state.
        component Tag: Rectangle {
            property alias text: label.text
            implicitWidth: label.implicitWidth + 2 * Theme.spacingM
            implicitHeight: label.implicitHeight + Theme.spacingXS
            radius: height / 2
            color: Theme.selected
            Text {
                id: label
                anchors.centerIn: parent
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.Medium
            }
        }
        // The selected window's full title, and where it is: its workspace, by name too when it
        // has one, and its output; then whether it is minimized or asking for attention.
        Column {
            id: caption
            anchors.top: grid.bottom; anchors.topMargin: Theme.spacingM
            x: switcher.padding; width: card.width - 2 * switcher.padding
            spacing: Theme.spacingS
            Text {
                width: parent.width
                text: switcher.current.title || switcher.current.appId || ""
                textFormat: Text.PlainText
                color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!switcher.current.output
                spacing: Theme.spacingS
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property string name: shell.workspaceNames[switcher.current.workspace - 1] || ""
                    text: "Workspace " + switcher.current.workspace + (name.length > 0 ? " · " + name : "")
                          + " · " + switcher.current.output
                    textFormat: Text.PlainText
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                }
                Tag {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: switcher.current.minimized === true
                    text: "Minimized"
                }
                Tag {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: switcher.current.urgent === true
                    text: "Needs attention"
                    color: Theme.urgentSubtle
                }
            }
        }
    }
}
