// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The start menu's first view: the applications pinned to it, six to a row, on pages of as many
// rows as there is room for, with "All apps ›" above them, and under them those launched lately
// with how long ago. Its content stays `inset` from its sides.
Item {
    id: home
    required property Item launcher
    property real inset: 0
    readonly property var pins: shell.startMenu.pinned
    readonly property int columns: 6
    readonly property real cellWidth: (width - 2 * inset) / columns
    readonly property real cellHeight: Theme.appIconSizeLarge + 2 * Theme.spacingL + Theme.spacingM + label.height
    readonly property real headingHeight: Theme.rowHeight
    readonly property real recentRowHeight: Theme.rowHeight + Theme.spacingXL
    // Up to three rows of pins, leaving the recent list two rows.
    readonly property int rows: Math.max(1, Math.min(3, Math.floor(
        (height - 2 * headingHeight - Theme.spacingL - 2 * recentRowHeight) / cellHeight)))
    readonly property int perPage: columns * rows
    readonly property int pages: Math.max(1, Math.ceil(pins.length / perPage))
    property int page: 0
    onPagesChanged: page = Math.min(page, pages - 1)
    // The recent list, two to a row, in what room is left.
    readonly property int recentRows: Math.max(1, Math.min(3, Math.floor(
        (height - recentList.y) / recentRowHeight)))
    readonly property var recent: shell.startMenu.recent.slice(0, 2 * recentRows)

    function reset() { page = 0 }

    Text { id: label; visible: false; text: "Ag"; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily }

    Item {
        id: pinnedHeading
        x: home.inset; width: home.width - 2 * home.inset; height: home.headingHeight
        Text {
            x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
            text: "Pinned"
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        StartButton {
            objectName: "startAllApps"
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            text: "All apps"
            onClicked: home.launcher.allApps = true
        }
    }
    // The pages lie one under the other; the one shown slides into place.
    Item {
        id: pinnedArea
        objectName: "startPinned"
        x: home.inset; y: pinnedHeading.height
        width: home.width - 2 * home.inset; height: home.rows * home.cellHeight
        clip: true
        Item {
            y: -home.page * pinnedArea.height
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Repeater {
                model: home.pins
                delegate: StartTile {
                    required property var modelData
                    required property int index
                    readonly property int place: index % home.perPage
                    app: modelData
                    x: place % home.columns * home.cellWidth
                    y: (Math.floor(index / home.perPage) * home.rows + Math.floor(place / home.columns)) * home.cellHeight
                    width: home.cellWidth; height: home.cellHeight
                    onClicked: home.launcher.launch(modelData.appId)
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: home.pins.length === 0
            text: "Pin applications here from their menus."
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        }
        // The wheel turns the pages, a notch (or a touchpad's worth of travel) a page.
        WheelHandler {
            enabled: home.pages > 1
            property real travel: 0
            onWheel: (event) => {
                travel += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                travel -= steps * 120
                if (steps !== 0)
                    home.page = Math.max(0, Math.min(home.pages - 1, home.page - steps))
            }
        }
    }
    // Which page is shown, in the margin beside them; a dot shows its page when clicked.
    Column {
        visible: home.pages > 1
        anchors.left: pinnedArea.right; anchors.leftMargin: (home.inset - Theme.spacingM) / 2
        anchors.verticalCenter: pinnedArea.verticalCenter
        spacing: Theme.spacingS
        Repeater {
            model: home.pages
            delegate: MouseArea {
                required property int index
                objectName: "startPage" + index
                width: Theme.spacingM; height: Theme.spacingL
                hoverEnabled: true
                onClicked: home.page = index
                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.spacingS + Theme.spacingXS; height: index === home.page ? Theme.spacingL : width
                    radius: width / 2
                    color: index === home.page ? Theme.accent : parent.containsMouse ? Theme.textMuted : Theme.textDisabled
                    Behavior on height { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                }
            }
        }
    }

    Item {
        id: recentHeading
        x: home.inset; y: pinnedArea.y + pinnedArea.height + Theme.spacingL
        width: home.width - 2 * home.inset; height: home.headingHeight
        Text {
            x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
            text: "Recent"
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
    }
    Grid {
        id: recentList
        objectName: "startRecent"
        x: home.inset; y: recentHeading.y + recentHeading.height
        width: home.width - 2 * home.inset
        columns: 2
        Repeater {
            model: home.recent
            delegate: StartRow {
                required property var modelData
                objectName: "startRecent:" + modelData.appId
                width: recentList.width / 2; height: home.recentRowHeight
                iconName: modelData.icon
                title: modelData.name
                subtitle: shell.startMenu.ago(modelData.launched, home.launcher.now)
                onClicked: home.launcher.launch(modelData.appId)
            }
        }
    }
    Text {
        x: home.inset + Theme.spacingM; y: recentList.y + Theme.spacingM
        width: home.width - 2 * home.inset - 2 * Theme.spacingM
        visible: home.recent.length === 0
        text: "The applications you open show up here."
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
    }
}
