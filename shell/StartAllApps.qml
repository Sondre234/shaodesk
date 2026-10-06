// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The start menu's list of every application from A to Z, under a heading for each letter, with
// "‹ Back" above it. Its content stays `inset` from its sides.
Item {
    id: all
    required property Item launcher
    property real inset: 0
    // The list: a {header} for each letter, then that letter's applications' records.
    readonly property var entries: {
        var list = [], letter = ""
        var apps = shell.startMenu.apps
        for (var i = 0; i < apps.length; ++i) {
            if (apps[i].letter !== letter) {
                letter = apps[i].letter
                list.push({ header: letter })
            }
            list.push(apps[i])
        }
        return list
    }
    readonly property real rowHeight: Theme.rowHeight + Theme.spacingS

    function reset() { list.positionViewAtBeginning() }

    Item {
        id: heading
        x: all.inset; width: all.width - 2 * all.inset; height: Theme.rowHeight
        Text {
            x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
            text: "All apps"
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        StartButton {
            objectName: "startBack"
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            back: true
            text: "Back"
            onClicked: all.launcher.allApps = false
        }
    }
    ListView {
        id: list
        objectName: "startAllList"
        x: all.inset; y: heading.height
        width: all.width - 2 * all.inset; height: all.height - y
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: all.entries
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            readonly property bool header: modelData.header !== undefined
            width: ListView.view.width
            height: header ? Theme.headingHeight : all.rowHeight
            Text {
                visible: entry.header
                objectName: entry.header ? "startLetter:" + entry.modelData.header : ""
                x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
                text: entry.header ? entry.modelData.header : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            StartRow {
                visible: !entry.header
                objectName: entry.header ? "" : "startApp:" + entry.modelData.appId
                anchors.fill: parent
                iconSize: Theme.appIconSize
                iconName: entry.header ? "" : entry.modelData.icon
                title: entry.header ? "" : entry.modelData.name
                onClicked: all.launcher.launch(entry.modelData.appId)
            }
        }
    }
}
