// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
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
        delegate: Button {
            id: workspaceButton
            required property int index
            readonly property int number: index + 1
            readonly property bool current: workspaceIndicator.workspaceState.current === number
            readonly property bool occupied: workspaceIndicator.workspaceState.occupied.indexOf(number) >= 0
            // A window on this workspace is asking for attention.
            readonly property bool urgent: (workspaceIndicator.workspaceState.urgent || []).indexOf(number) >= 0
            readonly property string label: shell.workspaceNames[index] || ""
            objectName: "workspace" + number
            width: label ? Math.max(26, workspaceText.implicitWidth + 14) : 26
            height: workspaceIndicator.barHeight - 14
            onClicked: { workspaceIndicator.panel.closeMenus(); workspaceIndicator.show(number) }
            Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
            BarTip { panel: workspaceIndicator.panel; owner: workspaceButton; text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "") }
            background: Rectangle {
                radius: 6
                color: workspaceButton.current ? Qt.lighter(shell.panelColor, 1.8) : (workspaceButton.hovered ? Qt.lighter(shell.panelColor, 1.4) : "transparent")
            }
            contentItem: Item {
                Text {
                    id: workspaceText
                    anchors.centerIn: parent
                    text: workspaceButton.label ? workspaceButton.label : workspaceButton.number
                    color: workspaceButton.urgent && !workspaceButton.current ? shell.urgentColor : workspaceButton.current ? shell.accent : shell.textColor
                    font.pixelSize: shell.fontSize; font.family: workspaceIndicator.panel.uiFont
                    font.weight: workspaceButton.current ? Font.DemiBold : Font.Normal
                }
                Rectangle {
                    objectName: "workspaceUrgent" + workspaceButton.number
                    visible: workspaceButton.urgent
                    anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 2
                    width: 6; height: 6; radius: 3
                    color: shell.urgentColor
                    SequentialAnimation on opacity {
                        running: workspaceButton.urgent
                        loops: 6; alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: 450 }
                        NumberAnimation { to: 1; duration: 450 }
                    }
                }
                Rectangle {
                    visible: workspaceButton.occupied
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                    width: 4; height: 4; radius: 2
                    color: workspaceButton.current ? shell.accent : shell.textColor
                }
            }
        }
    }
}
