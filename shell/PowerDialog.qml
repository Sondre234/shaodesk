// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The confirmation of power off, restart and log out, over a dimmed screen: the action goes
// ahead when the countdown runs out, on its button or on Enter; Escape, Cancel or a click
// beside the dialog gives it up. The screen dims and the dialog grows in each time it shows.
Item {
    id: root
    objectName: "powerDialog"
    readonly property var power: shell.power
    focus: true
    // How far it has come in, from 0 to 1: the scrim's and the dialog's opacity, and what is left
    // of the dialog's growth. It goes at once, as the action or the cancelling does.
    property real progress: 0
    states: State {
        name: "shown"
        when: root.Window.window !== null && root.Window.window.visible
        PropertyChanges { root.progress: 1 }
    }
    transitions: Transition {
        to: "shown"
        NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    Keys.onEscapePressed: root.power.cancel()
    Keys.onReturnPressed: root.power.confirm()
    Keys.onEnterPressed: root.power.confirm()

    function reset() { forceActiveFocus() }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.progress
    }
    MouseArea {
        objectName: "powerBackdrop"
        anchors.fill: parent
        onClicked: root.power.cancel()
    }
    Item {
        id: box
        objectName: "powerBox"
        anchors.centerIn: parent
        opacity: root.progress
        scale: 0.94 + 0.06 * root.progress
        width: Math.min(420, root.width - 2 * Theme.spacingXL)
        height: column.implicitHeight + 2 * column.anchors.margins
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.radiusLarge
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.surface
            border.color: Theme.border
        }
        // A click on the dialog itself gives nothing up.
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: column
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: Theme.spacingXXL
            spacing: Theme.spacingXL
            // The action's icon on a disc of the danger colour, beside its name and what is
            // about to happen.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingL
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: Theme.rowHeight + Theme.spacingS
                    Layout.preferredHeight: Theme.rowHeight + Theme.spacingS
                    radius: width / 2
                    color: Theme.dangerSurface
                    Icon {
                        anchors.centerIn: parent
                        name: root.power.pending === "reboot" ? "rotate-ccw"
                              : root.power.pending === "logout" ? "log-out" : "power"
                        size: Theme.iconSizeLarge; color: Theme.danger
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
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
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    }
                }
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
