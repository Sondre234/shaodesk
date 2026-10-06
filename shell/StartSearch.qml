// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// What the start menu's search finds for `query` (StartMenu.search), best first and in groups
// under their headings: the best match on a card of its own, then applications, open windows and
// actions. `current` is the result the keyboard is at, the best match to begin with, and `button`
// the best match's button it is at (-1 for none, the card itself). Its content stays `inset` from
// its sides.
Item {
    id: found
    required property Item launcher
    property real inset: 0
    property string query
    property int current: 0
    property int button: -1
    onCurrentChanged: button = -1
    readonly property var results: {
        shell.startMenu.apps // searched again as applications come and go
        return query === "" ? [] : shell.startMenu.search(query, shell.palette.entries(launcher.panel.taskSource))
    }
    onResultsChanged: { current = 0; button = -1 }
    readonly property var currentResult: results[current] || null
    // An application's record, when the keyboard is at one.
    readonly property var currentApp: currentResult && currentResult.kind === "app" ? currentResult : null
    readonly property var headings: ({ best: "Best match", apps: "Apps", windows: "Open windows", actions: "Actions" })
    // Where the pointer was last seen over the results.
    property point pointer: Qt.point(-1, -1)

    // The result the keyboard is at, or null.
    function currentItem() { return list.itemAtIndex(current) }
    // The best match's card, or null when there is none.
    function bestCard() {
        var first = list.itemAtIndex(0)
        return first && first.best ? first.card : null
    }
    // Down and Up move through every result, wrapping; Page Down and Page Up move a few. At the
    // best match, Right and Left move through its buttons, and Tab and Backtab through its
    // buttons too on their way through the results. Right only moves when `atEnd`, the text
    // cursor at the end of the search, where it would not move anyway. Returns whether the key
    // moved.
    function key(event, atEnd) {
        var count = results.length
        if (count === 0)
            return false
        var card = current === 0 ? bestCard() : null
        var buttons = card ? card.buttons : 0
        switch (event.key) {
        case Qt.Key_Right:
            if (!atEnd || button + 1 >= buttons)
                return false
            button++
            return true
        case Qt.Key_Left:
            if (button < 0)
                return false
            button--
            return true
        case Qt.Key_Tab:
            if (button + 1 < buttons)
                button++
            else
                current = (current + 1) % count
            return true
        case Qt.Key_Backtab:
            if (button >= 0) {
                button--
            } else {
                current = (current + count - 1) % count
                // Back onto the best match, at its last button.
                var back = current === 0 ? bestCard() : null
                if (back)
                    button = back.buttons - 1
            }
            return true
        case Qt.Key_Down: current = (current + 1) % count; return true
        case Qt.Key_Up: current = (current + count - 1) % count; return true
        case Qt.Key_PageDown: current = Math.min(count - 1, current + 5); return true
        case Qt.Key_PageUp: current = Math.max(0, current - 5); return true
        }
        return false
    }
    // Runs what the keyboard is at: the best match's button, or the result.
    function activate() {
        var card = current === 0 ? bestCard() : null
        if (card && button >= 0)
            card.press(button)
        else if (currentResult)
            launcher.run(currentResult)
    }

    // The pointer chooses the result it moves onto. Results appearing under a pointer that rests
    // there do not: the best match stays chosen, for Enter.
    HoverHandler {
        onPointChanged: {
            var at = point.scenePosition
            if (!hovered || (at.x === found.pointer.x && at.y === found.pointer.y))
                return
            var moved = found.pointer.x >= 0
            found.pointer = at
            var local = list.mapFromItem(null, at.x, at.y)
            var index = list.indexAt(local.x + list.contentX, local.y + list.contentY)
            if (moved && index >= 0)
                found.current = index
        }
    }
    ListView {
        id: list
        objectName: "startResults"
        x: found.inset; width: found.width - 2 * found.inset; height: found.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: found.results
        currentIndex: found.current
        highlightFollowsCurrentItem: false
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Column {
            id: result
            required property var modelData
            required property int index
            // The first of its group has the group's heading over it.
            readonly property bool first: index === 0 || found.results[index - 1].group !== modelData.group
            readonly property bool best: modelData.group === "best"
            readonly property Item card: bestMatch
            width: ListView.view.width
            Item {
                visible: result.first
                width: parent.width; height: Theme.rowHeight
                Text {
                    x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
                    text: found.headings[result.modelData.group] || ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                }
            }
            StartBestMatch {
                id: bestMatch
                visible: result.best
                width: parent.width; height: visible ? implicitHeight : 0
                result: result.modelData
                launcher: found.launcher
                current: found.current === result.index
                button: current ? found.button : -1
            }
            StartRow {
                id: row
                visible: !result.best
                objectName: result.best ? "" : "startResult:" + result.modelData.title
                width: parent.width; height: visible ? implicitHeight : 0
                iconName: result.modelData.icon || ""
                iconSize: Theme.appIconSize
                title: result.modelData.title
                subtitle: result.modelData.subtitle || ""
                current: found.current === result.index
                onClicked: found.launcher.run(result.modelData)
                onMenuRequested: (x, y) => {
                    if (result.modelData.kind === "app")
                        found.launcher.openAppMenu(result.modelData, row, x, y, false)
                }
            }
        }
    }
    Text {
        objectName: "startNothing"
        anchors.horizontalCenter: parent.horizontalCenter
        y: Theme.spacingXXL
        visible: found.query !== "" && found.results.length === 0
        text: "Nothing found for “" + found.query + "”"
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
    }
}
