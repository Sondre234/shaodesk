// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The time and date. Clicking it opens the month calendar.
Button {
    id: clockButton
    required property var panel
    required property real barHeight
    objectName: "clockButton"
    visible: shell.widgets.clock
    Layout.preferredWidth: clock.implicitWidth + 12; Layout.preferredHeight: clockButton.barHeight - 10
    hoverEnabled: true
    enabled: shell.widgets.calendar
    onClicked: clockButton.panel.toggleAudioPopup("calendar", clockButton)
    Accessible.name: Qt.formatDateTime(clock.now, "dddd d MMMM yyyy, HH:mm")
    background: Rectangle {
        radius: 7
        color: clockButton.panel.audioPopup === "calendar" ? Qt.lighter(shell.panelColor, 1.8) : (clockButton.hovered && clockButton.enabled ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
    }
    BarTip { panel: clockButton.panel; owner: clockButton; text: Qt.formatDate(clock.now, "dddd d MMMM yyyy") }
    contentItem: Text {
        id: clock
        objectName: "clock"
        property date now: new Date()
        text: Qt.formatTime(now, "HH:mm")
        color: shell.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        font.pixelSize: shell.fontSize; font.weight: Font.DemiBold; font.family: clockButton.panel.uiFont
        // The clock shows minutes, so it wakes once a minute, just after the minute changes.
        Timer {
            id: tick
            objectName: "clockTick"
            function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
            interval: untilMinute(); running: true; repeat: true
            onTriggered: { clock.now = new Date(); interval = untilMinute() }
        }
    }
}
