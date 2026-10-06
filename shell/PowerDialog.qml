// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The confirmation of power off, restart and log out, over a dimmed screen: the action goes
// ahead when the countdown runs out, on its button or on Enter; Escape, Cancel or a click
// beside the dialog gives it up.
Rectangle {
    id: root
    objectName: "powerDialog"
    readonly property var power: shell.power
    color: Theme.scrim
    focus: true
    Keys.onEscapePressed: root.power.cancel()
    Keys.onReturnPressed: root.power.confirm()
    Keys.onEnterPressed: root.power.confirm()

    function reset() { forceActiveFocus() }

    MouseArea {
        objectName: "powerBackdrop"
        anchors.fill: parent
        onClicked: root.power.cancel()
    }
    Rectangle {
        id: box
        objectName: "powerBox"
        anchors.centerIn: parent
        width: Math.min(400, root.width - 32)
        height: column.implicitHeight + 40
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
        // A click on the dialog itself gives nothing up.
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: column
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 20
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: root.power.pendingTitle
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
            }
            Text {
                objectName: "powerMessage"
                Layout.fillWidth: true
                text: root.power.message
                wrapMode: Text.WordWrap
                color: Theme.alpha(Theme.text, 0.8)
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize + 1
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    id: cancel
                    objectName: "powerCancel"
                    text: "Cancel"
                    palette.buttonText: Theme.text
                    onClicked: root.power.cancel()
                    background: Rectangle {
                        implicitWidth: 96; implicitHeight: 36; radius: Theme.radiusSmall
                        color: cancel.pressed ? Theme.pressed : cancel.hovered ? Theme.selected : Theme.hover
                    }
                }
                Button {
                    id: confirm
                    objectName: "powerConfirm"
                    text: root.power.pendingTitle
                    palette.buttonText: Theme.textOnDanger
                    onClicked: root.power.confirm()
                    background: Rectangle {
                        implicitWidth: 112; implicitHeight: 36; radius: Theme.radiusSmall
                        color: confirm.hovered ? Qt.lighter(Theme.dangerFill, 1.15) : Theme.dangerFill
                    }
                }
            }
        }
    }
}
