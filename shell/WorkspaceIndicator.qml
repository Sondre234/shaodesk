// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// This output's workspaces: the current one highlighted, a dot under those with
// windows. Clicking a number switches to it; scrolling pages through them, as it does
// anywhere on the bar.
Row {
    id: workspaceIndicator
    required property var panel
    required property real barHeight
    objectName: "workspaceIndicator"
    readonly property var workspaceState: shell.workspaces[workspaceIndicator.panel.outputName] || ({ current: 1, occupied: [] })
    function show(number) {
        if (number >= 1 && number <= shell.workspaceCount && number !== workspaceState.current)
            shell.showWorkspace(workspaceIndicator.panel.outputName, number)
    }
    visible: shell.workspaceCount > 1 && shell.widgets.workspaces
    spacing: 2
    Layout.alignment: Qt.AlignVCenter
    Repeater {
        model: shell.workspaceCount
        delegate: FlatButton {
            id: workspaceButton
            required property int index
            readonly property int number: index + 1
            readonly property bool current: workspaceIndicator.workspaceState.current === number
            readonly property bool occupied: workspaceIndicator.workspaceState.occupied.indexOf(number) >= 0
            // A window on this workspace is asking for attention.
            readonly property bool urgent: (workspaceIndicator.workspaceState.urgent || []).indexOf(number) >= 0
            readonly property string label: shell.workspaceNames[index] || ""
            objectName: "workspace" + number
            active: current
            width: label ? Math.max(26, workspaceText.implicitWidth + 14) : 26
            height: workspaceIndicator.barHeight - 14
            onClicked: { workspaceIndicator.panel.closeMenus(); workspaceIndicator.show(number) }
            Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
            BarTip { panel: workspaceIndicator.panel; owner: workspaceButton; text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "") }
            contentItem: Item {
                Text {
                    id: workspaceText
                    anchors.centerIn: parent
                    text: workspaceButton.label ? workspaceButton.label : workspaceButton.number
                    color: workspaceButton.urgent && !workspaceButton.current ? Theme.urgent : workspaceButton.current ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                    font.weight: workspaceButton.current ? Font.DemiBold : Font.Normal
                }
                Rectangle {
                    objectName: "workspaceUrgent" + workspaceButton.number
                    visible: workspaceButton.urgent
                    anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 2
                    width: 6; height: 6; radius: 3
                    color: Theme.urgent
                    SequentialAnimation on opacity {
                        running: workspaceButton.urgent
                        loops: 6; alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: Theme.duration(450) }
                        NumberAnimation { to: 1; duration: Theme.duration(450) }
                    }
                }
                Rectangle {
                    visible: workspaceButton.occupied
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                    width: 4; height: 4; radius: 2
                    color: workspaceButton.current ? Theme.accent : Theme.text
                }
            }
        }
    }
}
