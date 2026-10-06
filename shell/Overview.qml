// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The text of the overview. The compositor draws the thumbnails, the workspace strip and the
// selection; this sits over them in the output's coordinates with each window's title, the
// strip's workspace labels, the search box and a hint. It takes no input: the compositor keeps
// the keyboard and the pointer while the overview is open.
Item {
    id: overview
    required property size screenSize
    // The compositor's overview, unless set (as a preview sets them).
    property var windows: shell.overviewWindows
    property var strip: shell.overviewStrip
    property string filter: shell.overviewFilter
    property rect area: shell.overviewArea
    property int selected: shell.overviewSelected
    property int viewed: shell.overviewViewed
    // The workspaces with a window asking for attention.
    property var urgentWorkspaces: (shell.workspaces[shell.overviewOutput] || ({})).urgent || []
    width: screenSize.width
    height: screenSize.height

    // The search box: what was typed, or what typing does.
    Rectangle {
        id: search
        anchors.horizontalCenter: parent.horizontalCenter
        y: overview.area.y + 8
        width: Math.min(420, overview.width - 32); height: 32
        radius: 16
        color: Theme.surface
        border.color: overview.filter.length > 0 ? Theme.accent : Theme.border
        border.width: 1
        Text {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
            text: overview.filter.length > 0 ? overview.filter : qsTr("Type to search windows")
            color: overview.filter.length > 0 ? Theme.text : Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
        }
    }

    // Workspace labels over the strip's cells: the number or name, and how many windows.
    Repeater {
        model: overview.strip
        delegate: Item {
            id: cell
            required property var modelData
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            readonly property string name: shell.workspaceNames[modelData.workspace - 1] || ""
            // A window on this workspace is asking for attention.
            readonly property bool urgent: overview.urgentWorkspaces.indexOf(modelData.workspace) >= 0
            Rectangle {
                anchors.left: parent.left; anchors.bottom: parent.bottom
                anchors.margins: 4
                width: label.implicitWidth + 12; height: 18; radius: 9
                color: modelData.workspace === overview.viewed ? Theme.accent : Theme.alpha(Theme.surface, 0.85)
                border.width: cell.urgent ? 2 : 0; border.color: Theme.urgent
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: cell.name.length > 0 ? modelData.workspace + " " + cell.name : modelData.workspace
                    color: modelData.workspace === overview.viewed ? Theme.textOnAccent : Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; font.bold: true
                }
            }
        }
    }

    // Each window's title on a bar along the bottom of its thumbnail; the selected one's in full.
    Repeater {
        model: overview.windows
        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            readonly property bool selected: index === overview.selected
            readonly property string title: modelData.title.length > 0 ? modelData.title : modelData.appId
            visible: width >= 48
            // A window asking for attention has a frame in the urgent colour.
            Rectangle {
                objectName: "overviewUrgent"
                anchors.fill: parent
                visible: entry.modelData.urgent === true
                color: "transparent"; border.width: 3; border.color: Theme.urgent
            }
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 24
                color: Theme.alpha(Theme.surface, entry.selected ? 0.92 : 0.8)
                Image {
                    id: icon
                    x: 4; anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16; sourceSize: Qt.size(16, 16)
                    source: "image://icons/" + shell.iconFor(entry.modelData.appId)
                }
                Text {
                    anchors.left: icon.right; anchors.leftMargin: 6
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.title; textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.bold: entry.selected
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: overview.windows.length === 0
        text: overview.filter.length > 0 ? qsTr("No window matches") : qsTr("No windows here")
        color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: overview.area.y + overview.area.height - height - 4
        text: qsTr("Enter picks · Esc closes · middle click closes a window · drag a window onto a workspace to move it")
        color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
    }
}
