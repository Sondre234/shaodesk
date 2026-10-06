// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// What the start menu's search finds for `query` (StartMenu.search), best first and in groups
// under their headings: the best match on a card of its own, then applications, open windows and
// actions. `current` is the result the keyboard is at, the best match to begin with. Its content
// stays `inset` from its sides.
Item {
    id: found
    required property Item launcher
    property real inset: 0
    property string query
    property int current: 0
    readonly property var results: {
        shell.startMenu.apps // searched again as applications come and go
        return query === "" ? [] : shell.startMenu.search(query, shell.palette.entries(launcher.panel.taskSource))
    }
    onResultsChanged: current = 0
    readonly property var currentResult: results[current] || null
    readonly property var headings: ({ best: "Best match", apps: "Apps", windows: "Open windows", actions: "Actions" })

    // The result the keyboard is at, or null.
    function currentItem() { return list.itemAtIndex(current) }
    // An application's record, when the keyboard is at one.
    readonly property var currentApp: currentResult && currentResult.kind === "app" ? currentResult : null
    // Down and Tab, Up and Backtab move through every result, wrapping; Page Down and Page Up
    // move a few. Returns whether the key moved.
    function key(event) {
        var count = results.length
        if (count === 0)
            return false
        switch (event.key) {
        case Qt.Key_Down:
        case Qt.Key_Tab: current = (current + 1) % count; return true
        case Qt.Key_Up:
        case Qt.Key_Backtab: current = (current + count - 1) % count; return true
        case Qt.Key_PageDown: current = Math.min(count - 1, current + 5); return true
        case Qt.Key_PageUp: current = Math.max(0, current - 5); return true
        }
        return false
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
                visible: result.best
                width: parent.width; height: visible ? implicitHeight : 0
                result: result.modelData
                launcher: found.launcher
                current: found.current === result.index
                onHoveredChanged: if (hovered) found.current = result.index
            }
            StartRow {
                id: row
                visible: !result.best
                objectName: "startResult:" + result.modelData.title
                width: parent.width; height: visible ? implicitHeight : 0
                iconName: result.modelData.icon || ""
                iconSize: Theme.appIconSize
                title: result.modelData.title
                subtitle: result.modelData.subtitle || ""
                current: found.current === result.index
                onHoveredChanged: if (hovered) found.current = result.index
                onClicked: found.launcher.run(result.modelData)
                onMenuRequested: (x, y) => {
                    if (result.modelData.kind === "app")
                        found.launcher.openAppMenu(result.modelData, row, x, y, false)
                }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Theme.spacingXXL
        visible: found.query !== "" && found.results.length === 0
        text: "Nothing found for “" + found.query + "”"
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
    }
}
