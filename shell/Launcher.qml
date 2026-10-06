// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The application menu: a search field over the installed applications, which pin and unpin
// from here, and the power button in its bottom-right corner with its menu.
Rectangle {
    id: launcher
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "launcher"
    visible: panel.launcherOpen
    function opened() { search.text = ""; takeFocus() }
    // The keyboard goes to the power menu while it is open, else to the search field.
    function takeFocus() {
        if (panel.powerOpen) { powerMenu.current = 0; powerMenu.forceActiveFocus() }
        else search.forceActiveFocus()
    }
    onVisibleChanged: if (visible) opened()
    Component.onCompleted: if (visible) opened()
    Connections {
        target: launcher.panel
        function onPowerOpenChanged() { if (launcher.visible) launcher.takeFocus() }
    }
    width: Math.min(460, panel.width - 24)
    height: panel.height - shell.panelExtent - 20
    anchors.left: parent.left
    anchors.leftMargin: 12 + shell.panelMarginLeft
    y: panel.onTop ? barItem.y + barItem.height + 10 : barItem.y - height - 10
    color: shell.panelColor
    border.color: Qt.lighter(shell.panelColor, 1.65)
    radius: 14
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Applications"; color: shell.textColor; font.pixelSize: 21; font.weight: Font.DemiBold; font.family: panel.uiFont }
            Item { Layout.fillWidth: true }
            Button {
                text: "Refresh"
                onClicked: shell.refreshApps()
                palette.buttonText: shell.textColor
                background: Rectangle { color: parent.hovered ? "#304058" : "transparent"; radius: 6 }
            }
        }
        TextField {
            id: search
            objectName: "applicationSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            placeholderText: "Search applications"
            placeholderTextColor: Qt.darker(shell.textColor, 1.5)
            color: shell.textColor
            selectByMouse: true
            leftPadding: 12
            font.pixelSize: 14; font.family: panel.uiFont
            background: Rectangle {
                radius: 7
                color: Qt.darker(shell.panelColor, 1.2)
                border.color: search.activeFocus ? shell.accent : Qt.lighter(shell.panelColor, 1.7)
            }
            onAccepted: {
                if (applications.count > 0 && shell.launch(applications.model[0].appId)) panel.closeMenus()
            }
            Keys.onEscapePressed: panel.closeMenus()
        }
        ListView {
            id: applications
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 3
            model: shell.apps.filter(function(app) {
                return (app.name + " " + app.appId).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
            })
            ScrollBar.vertical: ScrollBar {}
            delegate: Button {
                required property var modelData
                width: ListView.view.width - 10
                height: 48
                onClicked: { if (shell.launch(modelData.appId)) panel.closeMenus() }
                background: Rectangle { radius: 7; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                contentItem: RowLayout {
                    spacing: 12
                    Image { source: "image://icons/" + modelData.icon; sourceSize: Qt.size(30, 30); Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                    Text { text: modelData.name; textFormat: Text.PlainText; color: shell.textColor; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true; font.family: panel.uiFont }
                    Text { visible: modelData.configured; text: "Pinned"; color: shell.accent; font.pixelSize: 10; font.family: panel.uiFont }
                    // Installed applications pin and unpin here; shown while hovered or pinned.
                    Button {
                        id: pinToggle
                        objectName: "pinToggle"
                        visible: !modelData.configured && (modelData.pinned || parent.parent.hovered || hovered)
                        text: modelData.pinned ? "Unpin" : "Pin"
                        Accessible.name: (modelData.pinned ? "Unpin " : "Pin ") + modelData.name + (modelData.pinned ? " from" : " to") + " taskbar"
                        onClicked: modelData.pinned ? shell.unpin(modelData.appId) : shell.pin(modelData.appId)
                        Layout.preferredHeight: 26
                        font.pixelSize: 11; font.family: panel.uiFont
                        palette.buttonText: modelData.pinned ? shell.accent : shell.textColor
                        background: Rectangle { radius: 5; color: pinToggle.hovered ? Qt.lighter(shell.panelColor, 1.9) : "transparent"; border.color: Qt.lighter(shell.panelColor, 1.9) }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: applications.count === 0; text: "No matching applications"; color: shell.textColor; font.family: panel.uiFont }
        }
        RowLayout {
            id: launcherFooter
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "shaodesk"
                color: Qt.darker(shell.textColor, 1.7)
                font.pixelSize: 11; font.family: panel.uiFont
            }
            // Lock, suspend and the rest, as far as the compositor says they may run.
            Button {
                id: powerButton
                objectName: "powerButton"
                visible: shell.widgets.power && shell.power.available.length > 0
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                onClicked: panel.powerOpen = !panel.powerOpen
                Accessible.name: "Power"
                ToolTip.text: "Lock, suspend, power off"
                ToolTip.visible: hovered && !panel.powerOpen
                ToolTip.delay: 500
                background: Rectangle {
                    radius: 7
                    color: panel.powerOpen ? Qt.lighter(shell.panelColor, 1.8) : (powerButton.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "power"; color: panel.powerOpen ? shell.accent : shell.textColor }
                }
            }
        }
    }
    // While the power menu is open, a press anywhere else in the launcher closes it.
    MouseArea {
        anchors.fill: parent
        z: 1
        visible: panel.powerOpen
        onPressed: panel.powerOpen = false
    }
    // The power menu, above the power button. Restart, power off and log out ask
    // first (PowerDialog.qml).
    Rectangle {
        id: powerMenu
        objectName: "powerMenu"
        z: 2
        // The entry Up and Down move to and Enter runs.
        property int current: 0
        function run(index) {
            var entry = shell.power.entries[index]
            // Closing the launcher first hands the keyboard back before it runs.
            panel.closeMenus()
            if (entry)
                shell.power.request(entry.action, launcher.panel.outputName)
        }
        visible: panel.powerOpen
        Keys.onUpPressed: current = (current + shell.power.entries.length - 1) % Math.max(1, shell.power.entries.length)
        Keys.onDownPressed: current = (current + 1) % Math.max(1, shell.power.entries.length)
        Keys.onReturnPressed: run(current)
        Keys.onEnterPressed: run(current)
        width: 220; height: 12 + shell.power.entries.length * 44 + Math.max(0, shell.power.entries.length - 1) * 2
        anchors.right: parent.right; anchors.rightMargin: 14
        anchors.bottom: parent.bottom; anchors.bottomMargin: 20 + launcherFooter.height + 6
        color: Qt.lighter(shell.panelColor, 1.2); radius: 10
        border.color: Qt.lighter(shell.panelColor, 1.8)
        MouseArea { anchors.fill: parent }
        Column {
            anchors.fill: parent; anchors.margins: 6; spacing: 2
            Repeater {
                model: shell.power.entries
                delegate: Button {
                    id: powerItem
                    required property var modelData
                    required property int index
                    objectName: "powerItem:" + modelData.action
                    width: parent.width; height: 44
                    text: modelData.title
                    focusPolicy: Qt.NoFocus
                    palette.buttonText: shell.textColor
                    onClicked: powerMenu.run(index)
                    onHoveredChanged: if (hovered) powerMenu.current = index
                    background: Rectangle { color: powerItem.hovered || powerMenu.current === powerItem.index ? Qt.lighter(shell.panelColor, 1.6) : "transparent"; radius: 6 }
                }
            }
        }
    }
}
