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
    // Where the keyboard is: a pin, then the recent applications after them; -1 for nowhere,
    // the search field keeping the keyboard. The page follows it.
    property int current: -1
    onCurrentChanged: if (current >= 0 && current < pins.length) page = Math.floor(current / perPage)
    // The record of the application the keyboard is at, or null.
    readonly property var currentApp: current < 0 ? null
                                    : current < pins.length ? pins[current] : recent[current - pins.length] || null

    function reset() {
        page = 0
        current = -1
    }
    // The tile or row the keyboard is at, or null.
    function currentItem() {
        return current < 0 ? null : current < pins.length ? tiles.itemAt(current)
                                                          : recentRepeater.itemAt(current - pins.length)
    }
    // Moves through the pins as they are laid out, on to the recent list under them and back;
    // Tab and Backtab go one by one. Returns whether the key moved.
    function key(event) {
        var count = pins.length, total = count + recent.length
        var at = current, pin = at >= 0 && at < count, column = at % columns
        switch (event.key) {
        case Qt.Key_Tab: current = at + 1 < total ? at + 1 : -1; return true
        case Qt.Key_Backtab: current = at < 0 ? total - 1 : at - 1; return true
        case Qt.Key_Right:
            if (at < 0)
                return false
            if (pin ? at + 1 < count : (at - count) % 2 === 0 && at + 1 < total)
                current = at + 1
            return true
        case Qt.Key_Left:
            if (at < 0)
                return false
            if (pin ? at > 0 : (at - count) % 2 === 1)
                current = at - 1
            return true
        case Qt.Key_Down:
            if (at < 0)
                current = count > 0 ? page * perPage : total > 0 ? 0 : -1
            else if (pin && at + columns < count)
                current = at + columns
            else if (pin && Math.floor(at / columns) < Math.floor((count - 1) / columns))
                current = count - 1 // into the last row, which is not full
            else if (pin && recent.length > 0)
                current = count + Math.min(column < columns / 2 ? 0 : 1, recent.length - 1)
            else if (!pin && at + 2 < total)
                current = at + 2
            return true
        case Qt.Key_Up:
            if (at < 0)
                return false
            if (pin)
                current = at - columns >= 0 ? at - columns : -1
            else if (at - 2 >= count)
                current = at - 2
            else if (count > 0) {
                // Into the last row of the page shown, on its side of the list.
                var last = Math.min(count, (page + 1) * perPage) - 1
                current = Math.min(count - 1, last - last % columns + ((at - count) % 2) * columns / 2)
            } else
                current = -1
            return true
        }
        return false
    }

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
                id: tiles
                model: home.pins
                delegate: StartTile {
                    id: tile
                    required property var modelData
                    required property int index
                    readonly property int place: index % home.perPage
                    app: modelData
                    current: home.current === index
                    x: place % home.columns * home.cellWidth
                    y: (Math.floor(index / home.perPage) * home.rows + Math.floor(place / home.columns)) * home.cellHeight
                    width: home.cellWidth; height: home.cellHeight
                    onClicked: home.launcher.launch(modelData.appId)
                    onMenuRequested: (x, y) => home.launcher.openAppMenu(modelData, tile, x, y, true)
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
            id: recentRepeater
            model: home.recent
            delegate: StartRow {
                id: recentRow
                required property var modelData
                required property int index
                objectName: "startRecent:" + modelData.appId
                current: home.current === home.pins.length + index
                width: recentList.width / 2; height: home.recentRowHeight
                iconName: modelData.icon
                title: modelData.name
                subtitle: shell.startMenu.ago(modelData.launched, home.launcher.now)
                onClicked: home.launcher.launch(modelData.appId)
                onMenuRequested: (x, y) => home.launcher.openAppMenu(modelData, recentRow, x, y, false)
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
