// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import Shaodesk

// The windows of the taskbar button the pointer rests on, with shell.thumbnails: a tile for each,
// side by side in the order the stack's list has them, with the application's icon, the window's
// title and, while the pointer is on the tile, a cross that closes the window, over a small
// picture of it. The picture is the task model's (its `picture` role, while the tile wants it
// through the panel's wantPicture) and follows the window while shown with
// shell.liveThumbnails; until one has come, or without one, the application's icon stands in for
// it. The focused window's tile is marked as the bar marks it: selected, with a line in the
// accent colour (the urgent one for a window asking for attention) under it. Clicking a tile
// focuses its window (or minimizes it when focused already), the cross or a middle click closes
// it, and a right click opens its menu. How wide the pictures are, and whether there is room for
// them at all rather than the list, is the panel's (thumbnailWidth).
PopupCard {
    id: thumbnails
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "windowThumbnails"
    open: panel.thumbnailsOpen
    // Shown on hover, it leaves the keyboard where it is.
    initialFocus: null
    readonly property TaskFilter windows: panel.groupWindows
    readonly property real tileGap: panel.thumbnailGap
    readonly property real pictureWidth: panel.thumbnailWidth
    readonly property real pictureHeight: Math.round(pictureWidth * 0.625)
    // A tile's header is as tall as the cross in it.
    readonly property real headerHeight: Theme.iconSize + Theme.spacingS
    readonly property real tileWidth: pictureWidth + 2 * panel.thumbnailPadding
    readonly property real tileHeight: 2 * panel.thumbnailPadding + headerHeight + Theme.spacingS + pictureHeight
    readonly property int count: Math.max(1, windows.count)
    implicitWidth: 2 * tileGap + count * tileWidth + (count - 1) * tileGap
    implicitHeight: 2 * tileGap + tileHeight
    anchorRect: panel.dockAnchor(panel.groupX, 0)
    side: panel.dockSide
    bounds: panel.popupArea
    HoverHandler {
        id: hover
        onHoveredChanged: thumbnails.panel.hoverGroupList(hovered)
    }
    readonly property bool hovered: hover.hovered
    // The last of its windows closing closes it.
    Connections {
        target: thumbnails.windows
        function onCountChanged() {
            if (thumbnails.windows.count === 0 && thumbnails.open)
                thumbnails.panel.groupOpen = false
        }
    }
    Row {
        anchors.fill: parent; anchors.margins: thumbnails.tileGap
        spacing: thumbnails.tileGap
        Repeater {
            model: thumbnails.visible ? thumbnails.windows : null
            delegate: Button {
                id: tile
                required property int taskId
                required property string title
                required property string appId
                required property bool active
                required property bool minimized
                required property bool urgent
                // The row itself, for its picture, which a stand-in model may not have.
                required property var model
                objectName: "windowThumbnail"
                width: thumbnails.tileWidth; height: thumbnails.tileHeight
                padding: thumbnails.panel.thumbnailPadding
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: title
                // Closing the card destroys this tile, so it goes last.
                onClicked: { thumbnails.panel.taskSource.activate(taskId); thumbnails.panel.groupOpen = false }
                // The tile wants its window's picture while it is there (the panel's
                // wantPicture), taking over from the panel's own want as the card opens: the
                // window it wanted, of the panel it asked.
                property Item owner: null
                property int watched: -1
                Component.onCompleted: {
                    owner = thumbnails.panel
                    watched = taskId
                    owner.wantPicture(watched, true)
                }
                Component.onDestruction: if (owner) owner.wantPicture(watched, false)
                readonly property bool marked: active || urgent
                background: ButtonFill {
                    hovered: tile.hovered
                    pressed: tile.pressed
                    active: tile.active
                    Rectangle {
                        objectName: "windowThumbnailLine"
                        visible: tile.marked
                        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spacingXS
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.iconSizeSmall; height: 3; radius: 1
                        color: tile.urgent ? Theme.urgent : Theme.accent
                    }
                }
                contentItem: ColumnLayout {
                    spacing: Theme.spacingS
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: thumbnails.headerHeight
                        spacing: Theme.spacingS
                        Image {
                            Layout.leftMargin: Theme.spacingXS
                            Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                            source: "image://icons/" + thumbnails.panel.groupIcon
                            sourceSize: Qt.size(Theme.iconSizeSmall, Theme.iconSizeSmall)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tile.title; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: tile.urgent ? Theme.urgent : tile.minimized ? Theme.textMuted : Theme.text
                            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        }
                        // Always in its place, so that the title does not move as it shows.
                        CloseButton {
                            id: closeButton
                            objectName: "windowThumbnailClose"
                            readonly property bool shown: tile.hovered || hovered
                            opacity: shown ? 1 : 0
                            enabled: shown
                            danger: true
                            Layout.preferredWidth: size; Layout.preferredHeight: size
                            Accessible.name: "Close " + tile.title
                            onClicked: thumbnails.panel.taskSource.close(tile.taskId)
                        }
                    }
                    // The picture, as large as fits in its box, keeping its proportions; with
                    // the GPU its corners are rounded as the box's.
                    Item {
                        id: box
                        Layout.preferredWidth: thumbnails.pictureWidth; Layout.preferredHeight: thumbnails.pictureHeight
                        Rectangle {
                            objectName: "windowThumbnailStandIn"
                            anchors.fill: parent
                            visible: !picture.visible
                            radius: Theme.radiusSmall
                            color: Theme.alpha(Theme.text, 0.06)
                            Image {
                                anchors.centerIn: parent
                                width: Theme.appIconSizeLarge; height: Theme.appIconSizeLarge
                                source: "image://icons/" + thumbnails.panel.groupIcon
                                sourceSize: Qt.size(Theme.appIconSizeLarge, Theme.appIconSizeLarge)
                                opacity: tile.minimized ? 0.5 : 1
                            }
                        }
                        Image {
                            id: picture
                            objectName: "windowThumbnailPicture"
                            readonly property real aspect: implicitHeight > 0 ? implicitWidth / implicitHeight : 1
                            anchors.centerIn: parent
                            width: Math.min(box.width, box.height * aspect)
                            height: Math.min(box.height, box.width / aspect)
                            source: tile.model.picture || ""
                            // A new picture has a new name; the old one is not wanted again, but
                            // stays until the new one has loaded.
                            cache: false
                            retainWhileLoading: true
                            smooth: true; mipmap: true
                            // Whether it has a picture to show: once one has loaded, until there
                            // is none.
                            property bool shown: false
                            onStatusChanged: {
                                if (status === Image.Ready) shown = true
                                else if (status !== Image.Loading) shown = false
                            }
                            visible: shown || status === Image.Ready
                            layer.enabled: Theme.effects
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: pictureMask
                                maskThresholdMin: 0.5
                                maskSpreadAtMin: 1
                            }
                        }
                        Item {
                            id: pictureMask
                            anchors.fill: picture
                            visible: false
                            layer.enabled: Theme.effects
                            Rectangle { anchors.fill: parent; radius: Theme.radiusSmall }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            thumbnails.panel.openContextMenu(tile, 0, tile.taskId, tile.appId)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) thumbnails.panel.taskSource.close(tile.taskId)
                    }
                }
            }
        }
    }
}
