// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The power menu: lock, suspend and the rest, as far as the compositor says they may run. Up and
// Down choose, Enter runs. Restart, power off and log out ask first (PowerDialog.qml).
Rectangle {
    id: powerMenu
    required property var panel
    objectName: "powerMenu"
    // The entry Up and Down move to and Enter runs.
    property int current: 0
    function run(index) {
        var entry = shell.power.entries[index]
        // Closing the launcher first hands the keyboard back before it runs.
        panel.closeMenus()
        if (entry)
            shell.power.request(entry.action, powerMenu.panel.outputName)
    }
    visible: panel.powerOpen
    Keys.onUpPressed: current = (current + shell.power.entries.length - 1) % Math.max(1, shell.power.entries.length)
    Keys.onDownPressed: current = (current + 1) % Math.max(1, shell.power.entries.length)
    Keys.onReturnPressed: run(current)
    Keys.onEnterPressed: run(current)
    width: 220; height: 12 + shell.power.entries.length * 44 + Math.max(0, shell.power.entries.length - 1) * 2
    color: Theme.surfaceRaised; radius: Theme.radiusMedium
    border.color: Theme.border
    MouseArea { anchors.fill: parent }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 2
        Repeater {
            model: shell.power.entries
            delegate: FlatButton {
                id: powerItem
                required property var modelData
                required property int index
                objectName: "powerItem:" + modelData.action
                width: parent.width; height: 44
                text: modelData.title
                focusPolicy: Qt.NoFocus
                palette.buttonText: Theme.text
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                active: powerMenu.current === index
                onClicked: powerMenu.run(index)
                onHoveredChanged: if (hovered) powerMenu.current = index
            }
        }
    }
}
