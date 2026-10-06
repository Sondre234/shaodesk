// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The application menu: a search field over the installed applications, which pin and unpin
// from here, and the power button in its bottom-right corner with its menu.
PopupCard {
    id: launcher
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "launcher"
    open: panel.launcherOpen
    // The keyboard goes to the power menu while it is open, else to the search field.
    initialFocus: null
    onOpened: { search.text = ""; takeFocus() }
    function takeFocus() {
        if (panel.powerOpen) { powerMenu.current = 0; powerMenu.forceActiveFocus() }
        else search.forceActiveFocus()
    }
    Connections {
        target: launcher.panel
        function onPowerOpenChanged() { if (launcher.open) launcher.takeFocus() }
    }
    // By the bar's start, up to 720 pixels tall as the output leaves room for.
    implicitWidth: 460
    implicitHeight: 720
    anchorRect: panel.barAnchor(12 + shell.panelMarginLeft, 0)
    alignment: Qt.AlignLeft
    side: panel.popupSide
    gap: 10
    margin: 10
    radius: Theme.radiusLarge
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Applications"; color: Theme.text; font.pixelSize: Theme.fontSizeDisplay; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: "Refresh"
                onClicked: shell.refreshApps()
                palette.buttonText: Theme.text
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
            }
        }
        TextField {
            id: search
            objectName: "applicationSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            placeholderText: "Search applications"
            placeholderTextColor: Theme.textMuted
            color: Theme.text
            selectByMouse: true
            leftPadding: 12
            font.pixelSize: Theme.fontSizeLarge; font.family: Theme.fontFamily
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: search.activeFocus ? Theme.accent : Theme.border
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
            delegate: FlatButton {
                required property var modelData
                width: ListView.view.width - 10
                height: 48
                onClicked: { if (shell.launch(modelData.appId)) panel.closeMenus() }
                contentItem: RowLayout {
                    spacing: 12
                    Image { source: "image://icons/" + modelData.icon; sourceSize: Qt.size(Theme.appIconSizeLarge, Theme.appIconSizeLarge); Layout.preferredWidth: Theme.appIconSizeLarge; Layout.preferredHeight: Theme.appIconSizeLarge }
                    Text { text: modelData.name; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.fontSizeLarge; elide: Text.ElideRight; Layout.fillWidth: true; font.family: Theme.fontFamily }
                    Text { visible: modelData.configured; text: "Pinned"; color: Theme.accent; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily }
                    // Installed applications pin and unpin here; shown while hovered or pinned.
                    Button {
                        id: pinToggle
                        objectName: "pinToggle"
                        visible: !modelData.configured && (modelData.pinned || parent.parent.hovered || hovered)
                        text: modelData.pinned ? "Unpin" : "Pin"
                        Accessible.name: (modelData.pinned ? "Unpin " : "Pin ") + modelData.name + (modelData.pinned ? " from" : " to") + " taskbar"
                        onClicked: modelData.pinned ? shell.unpin(modelData.appId) : shell.pin(modelData.appId)
                        Layout.preferredHeight: 26
                        font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        palette.buttonText: modelData.pinned ? Theme.accent : Theme.text
                        background: Rectangle { radius: Theme.radiusSmall; color: pinToggle.hovered ? Theme.hover : "transparent"; border.color: Theme.border }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: applications.count === 0; text: "No matching applications"; color: Theme.textMuted; font.family: Theme.fontFamily }
        }
        RowLayout {
            id: launcherFooter
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "shaodesk"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
            // Lock, suspend and the rest, as far as the compositor says they may run.
            FlatButton {
                id: powerButton
                objectName: "powerButton"
                visible: shell.widgets.power && shell.power.available.length > 0
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                active: launcher.panel.powerOpen
                onClicked: panel.powerOpen = !panel.powerOpen
                Accessible.name: "Power"
                BarTip {
                    panel: launcher.panel; owner: powerButton
                    visible: powerButton.hovered && !launcher.panel.powerOpen
                    text: "Lock, suspend, power off"
                }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "power"; color: panel.powerOpen ? Theme.accent : Theme.text }
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
    // The power menu, above the power button.
    PowerMenu {
        id: powerMenu
        panel: launcher.panel
        z: 2
        anchors.right: parent.right; anchors.rightMargin: 14
        anchors.bottom: parent.bottom; anchors.bottomMargin: 20 + launcherFooter.height + 6
    }
}
