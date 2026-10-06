// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The start menu, after Windows 11's: a search field on top, the applications under it, and
// along the bottom who is logged in and the power button, in the bottom-right corner, with its
// menu. The applications are read again as they are installed and removed.
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
        if (panel.powerOpen) powerMenu.forceActiveFocus()
        else search.forceActiveFocus()
    }
    Connections {
        target: launcher.panel
        function onPowerOpenChanged() { if (launcher.open) launcher.takeFocus() }
    }
    // By the bar's start, 640 by 720 pixels, or as tall as the output leaves room for.
    implicitWidth: 640
    implicitHeight: 720
    anchorRect: panel.barAnchor(12 + shell.panelMarginLeft, 0)
    alignment: Qt.AlignLeft
    side: panel.popupSide
    gap: 10
    margin: 10
    radius: Theme.radiusLarge
    // The space between the card's edges and what is on it.
    readonly property real padding: Theme.spacingXXL + Theme.spacingL

    TextField {
        id: search
        objectName: "applicationSearch"
        x: launcher.padding; y: launcher.padding
        width: launcher.width - 2 * launcher.padding
        height: Theme.rowHeight + Theme.spacingS
        leftPadding: Theme.spacingL + Theme.iconSizeSmall + Theme.spacingM
        rightPadding: Theme.spacingL
        placeholderText: "Search apps, windows and actions"
        placeholderTextColor: Theme.textMuted
        color: Theme.text
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        background: Rectangle {
            radius: height / 2
            color: Theme.surfaceRaised
            border.color: search.activeFocus ? Theme.accent : Theme.border
            Icon {
                x: Theme.spacingL; anchors.verticalCenter: parent.verticalCenter
                name: "search"; size: Theme.iconSizeSmall; color: Theme.textMuted
            }
        }
        onAccepted: {
            if (applications.count > 0 && shell.launch(applications.model[0].appId)) panel.closeMenus()
        }
        Keys.onEscapePressed: panel.closeMenus()
    }
    ListView {
        id: applications
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: search.bottom; anchors.bottom: footer.top
        anchors.leftMargin: launcher.padding - Theme.spacingM; anchors.rightMargin: anchors.leftMargin
        anchors.topMargin: Theme.spacingXL; anchors.bottomMargin: Theme.spacingM
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
            }
        }
        Text { anchors.centerIn: parent; visible: applications.count === 0; text: "No matching applications"; color: Theme.textMuted; font.family: Theme.fontFamily }
    }
    // Along the bottom, in a shade of its own: who is logged in, and the power button.
    Rectangle {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        // Inside the card's outline.
        anchors.margins: 1
        height: 2 * Theme.rowHeight - Theme.spacingM
        color: Theme.surfaceRaised
        bottomLeftRadius: launcher.radius - 1; bottomRightRadius: launcher.radius - 1
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        UserAvatar {
            id: avatar
            x: launcher.padding; anchors.verticalCenter: parent.verticalCenter
            size: Theme.appIconSizeLarge
            backdrop: footer.color
        }
        Text {
            objectName: "userName"
            anchors.left: avatar.right; anchors.leftMargin: Theme.spacingL
            anchors.right: powerButton.left; anchors.rightMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            text: shell.startMenu.userName; textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        }
        // Lock, suspend and the rest, as far as the compositor says they may run.
        FlatButton {
            id: powerButton
            objectName: "powerButton"
            visible: shell.widgets.power && shell.power.available.length > 0
            anchors.right: parent.right; anchors.rightMargin: launcher.padding - Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.rowHeight; height: Theme.rowHeight
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
    // While the power menu is open, a press anywhere else in the launcher closes it.
    MouseArea {
        anchors.fill: parent
        z: 1
        visible: panel.powerOpen
        onPressed: panel.powerOpen = false
    }
    // The power menu, above the power button and ending where it ends, over the launcher.
    PowerMenu {
        id: powerMenu
        panel: launcher.panel
        parent: launcher.panel.popupLayer
        z: 1
        anchorRect: Qt.rect(launcher.x + footer.x + powerButton.x, launcher.y + footer.y + powerButton.y,
                            powerButton.width, powerButton.height)
        side: Qt.TopEdge
        alignment: Qt.AlignRight
        gap: 6
        bounds: launcher.panel.popupArea
    }
}
