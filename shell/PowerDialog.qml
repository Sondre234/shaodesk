// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The confirmation of power off, restart and log out, over a dimmed screen: the action goes
// ahead when the countdown runs out, on its button or on Enter; Escape, Cancel or a click
// beside the dialog gives it up. The keyboard starts on the action's button, and Tab moves it
// to Cancel and back, Enter pressing the one it is on. The screen dims and the dialog grows in
// each time it shows.
Item {
    id: root
    objectName: "powerDialog"
    readonly property var power: shell.power
    focus: true
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the scrim's and the dialog's opacity, and what is left of
    // the dialog's growth. It goes quicker than it came.
    property bool shown: false
    property real progress: 0
    states: State {
        name: "shown"
        when: root.shown
        PropertyChanges { root.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]
    // What it asks about, held as it was while it goes: the question is over by then.
    property string pending: ""
    property string pendingTitle: ""
    property string message: ""
    Binding on pending { when: root.shown; value: root.power.pending; restoreMode: Binding.RestoreNone }
    Binding on pendingTitle { when: root.shown; value: root.power.pendingTitle; restoreMode: Binding.RestoreNone }
    Binding on message { when: root.shown; value: root.power.message; restoreMode: Binding.RestoreNone }
    Keys.onEscapePressed: root.power.cancel()
    Keys.onReturnPressed: cancel.activeFocus ? root.power.cancel() : root.power.confirm()
    Keys.onEnterPressed: cancel.activeFocus ? root.power.cancel() : root.power.confirm()

    function reset() { confirm.forceActiveFocus() }

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
                        name: root.pending === "reboot" ? "rotate-ccw"
                              : root.pending === "logout" ? "log-out" : "power"
                        size: Theme.iconSizeLarge; color: Theme.danger
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Text {
                        Layout.fillWidth: true
                        text: root.pendingTitle
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                    }
                    Text {
                        objectName: "powerMessage"
                        Layout.fillWidth: true
                        text: root.message
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingM
                DialogButton {
                    id: cancel
                    objectName: "powerCancel"
                    text: "Cancel"
                    onClicked: root.power.cancel()
                }
                DialogButton {
                    id: confirm
                    objectName: "powerConfirm"
                    text: root.pendingTitle
                    danger: true
                    onClicked: root.power.confirm()
                }
            }
        }
    }

    // A button of the dialog: framed on the raised surface, or for the action itself, which
    // cannot be taken back, filled with the danger colour. A ring in the accent colour around it
    // says that the keyboard is on it.
    component DialogButton: Button {
        id: button
        property bool danger: false
        implicitWidth: Math.max(contentItem.implicitWidth + leftPadding + rightPadding, 3 * Theme.rowHeight)
        implicitHeight: Theme.rowHeight
        leftPadding: Theme.spacingXL; rightPadding: Theme.spacingXL
        hoverEnabled: true
        background: Rectangle {
            radius: Theme.radiusSmall
            color: button.danger ? (button.hovered ? Theme.mix(Theme.dangerFill, Theme.textOnDanger, 0.12) : Theme.dangerFill)
                   : button.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
            border.color: button.danger ? "transparent" : Theme.border
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: button.pressed ? Theme.pressed : "transparent"
            }
            Rectangle {
                visible: button.activeFocus
                anchors.fill: parent; anchors.margins: -Theme.spacingS
                radius: parent.radius + Theme.spacingS
                color: "transparent"
                border.color: Theme.accent; border.width: 2
            }
        }
        contentItem: Text {
            text: button.text
            color: button.danger ? Theme.textOnDanger : Theme.text
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }
}
