// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The time and date. Clicking it opens the clock flyout: the notifications and the month
// calendar.
FlatButton {
    id: clockButton
    required property var panel
    required property real barHeight
    objectName: "clockButton"
    visible: shell.widgets.clock
    Layout.preferredWidth: clock.implicitWidth + 12; Layout.preferredHeight: clockButton.barHeight - 10
    hoverEnabled: true
    enabled: shell.widgets.calendar || shell.notifications.serving
    onClicked: clockButton.panel.toggleAudioPopup("clock", clockButton)
    Accessible.name: Qt.formatDateTime(clock.now, "dddd d MMMM yyyy, HH:mm")
    active: clockButton.panel.audioPopup === "clock"
    BarTip { panel: clockButton.panel; owner: clockButton; text: Qt.formatDate(clock.now, "dddd d MMMM yyyy") }
    // The compositor's notification_history action opens the flyout on the output under the
    // pointer, whether or not the clock is on the bar.
    Connections {
        target: shell
        function onNotificationsRequested(output) {
            if (output === clockButton.panel.outputName) clockButton.panel.toggleAudioPopup("clock", clockButton)
        }
    }
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
