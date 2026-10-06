// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// An application's status icon in the tray (a StatusNotifierItem), its attention icon while
// it needs attention. Left-click activates the application (or opens the menu of an item that
// is only a menu), middle-click is its secondary action, right-click opens its menu, and the
// wheel scrolls it.
Button {
    id: trayButton
    required property string key
    required property string title
    required property string status
    required property string image
    required property string toolTip
    required property bool itemIsMenu
    required property bool hasMenu
    // The panel it is on, which opens its menu.
    required property Item panel
    objectName: "trayItem"
    visible: status !== "Passive"
    width: 32; height: shell.panelHeight - 10
    hoverEnabled: true
    Accessible.name: title
    onClicked: panel.trayActivate(trayButton)
    ToolTip {
        id: trayTip
        visible: trayButton.hovered && !trayButton.panel.menuOpen && !trayButton.pressed && text.length > 0
        delay: 500
        text: trayButton.toolTip
        width: Math.min(implicitWidth, 420)
        // An application's text, shown as it is rather than read as markup.
        contentItem: Text { text: trayTip.text; textFormat: Text.PlainText; font: trayTip.font; wrapMode: Text.Wrap; color: trayTip.palette.toolTipText }
        y: trayButton.panel.onTop ? trayButton.height + 6 : -implicitHeight - 6
        Component.onCompleted: if ("popupType" in trayTip) trayTip.popupType = Popup.Window
    }
    background: Rectangle {
        radius: 7
        color: trayButton.panel.trayMenuKey === trayButton.key ? Qt.lighter(shell.panelColor, 1.8)
               : trayButton.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent"
    }
    contentItem: Item {
        Image {
            objectName: "trayIcon"
            readonly property int size: Math.max(12, Math.min(20, shell.panelHeight - 30))
            anchors.centerIn: parent
            width: size; height: size
            source: trayButton.image; sourceSize: Qt.size(size, size)
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                trayButton.panel.trayMenu(trayButton)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton)
                trayButton.panel.traySecondary(trayButton)
        }
    }
    // A notch at a time, in Qt's units (120 a notch), as KDE's tray sends it.
    WheelHandler {
        property real travel: 0
        onWheel: (event) => {
            var horizontal = event.angleDelta.y === 0
            travel += horizontal ? event.angleDelta.x : event.angleDelta.y
            var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
            travel -= steps * 120
            if (steps !== 0)
                shell.tray.scroll(trayButton.key, steps * 120, horizontal)
        }
    }
}
