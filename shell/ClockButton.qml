// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The time and date. Clicking it opens the month calendar.
FlatButton {
    id: clockButton
    required property var panel
    required property real barHeight
    objectName: "clockButton"
    visible: shell.widgets.clock
    Layout.preferredWidth: clock.implicitWidth + 12; Layout.preferredHeight: clockButton.barHeight - 10
    enabled: shell.widgets.calendar
    onClicked: clockButton.panel.toggleAudioPopup("calendar", clockButton)
    Accessible.name: Qt.formatDateTime(clock.now, "dddd d MMMM yyyy, HH:mm")
    active: clockButton.panel.audioPopup === "calendar"
    BarTip { panel: clockButton.panel; owner: clockButton; text: Qt.formatDate(clock.now, "dddd d MMMM yyyy") }
    contentItem: Text {
        id: clock
        objectName: "clock"
        property date now: new Date()
        text: Qt.formatTime(now, "HH:mm")
        color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
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
