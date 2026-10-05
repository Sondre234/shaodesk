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
    readonly property string uiFont: shell.fontFamily.length > 0 ? shell.fontFamily : Qt.application.font.family
    color: Qt.rgba(0, 0, 0, 0.45)
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
        radius: 14
        color: shell.panelColor
        border.color: Qt.lighter(shell.panelColor, 1.6)
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
                color: shell.textColor
                font.family: root.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold
            }
            Text {
                objectName: "powerMessage"
                Layout.fillWidth: true
                text: root.power.message
                wrapMode: Text.WordWrap
                color: Qt.rgba(shell.textColor.r, shell.textColor.g, shell.textColor.b, 0.8)
                font.family: root.uiFont; font.pixelSize: shell.fontSize + 1
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    id: cancel
                    objectName: "powerCancel"
                    text: "Cancel"
                    palette.buttonText: shell.textColor
                    onClicked: root.power.cancel()
                    background: Rectangle {
                        implicitWidth: 96; implicitHeight: 36; radius: 8
                        color: cancel.hovered ? Qt.lighter(shell.panelColor, 1.6) : Qt.lighter(shell.panelColor, 1.3)
                    }
                }
                Button {
                    id: confirm
                    objectName: "powerConfirm"
                    text: root.power.pendingTitle
                    palette.buttonText: "#ffffff"
                    onClicked: root.power.confirm()
                    background: Rectangle {
                        implicitWidth: 112; implicitHeight: 36; radius: 8
                        color: confirm.hovered ? Qt.lighter("#c4443c", 1.15) : "#c4443c"
                    }
                }
            }
        }
    }
}
