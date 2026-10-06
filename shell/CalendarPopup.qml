// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock flyout's calendar card (ClockFlyout.qml): the time and today's date over a month
// calendar with today marked, paged by month. Weeks start on the locale's first day.
PopupCard {
    id: calendar
    required property var panel
    objectName: "calendar"
    property int month: new Date().getMonth()
    property int year: new Date().getFullYear()
    // Now, to the minute, while it shows.
    property date now: new Date()
    function step(delta) {
        var d = new Date(year, month + delta, 1)
        year = d.getFullYear(); month = d.getMonth()
    }
    function today() { var d = new Date(); year = d.getFullYear(); month = d.getMonth() }
    onOpened: { now = new Date(); today() }
    // A day's cell, and the room around the grid.
    readonly property real cellWidth: Theme.rowHeight + Theme.spacingL
    readonly property real cellHeight: Theme.rowHeight + Theme.spacingS
    readonly property real padding: Theme.spacingXL
    implicitWidth: 7 * cellWidth + 2 * padding
    implicitHeight: content.implicitHeight + 2 * padding
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    Timer {
        function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
        interval: untilMinute(); running: calendar.visible; repeat: true
        onTriggered: { calendar.now = new Date(); interval = untilMinute() }
    }
    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: calendar.padding
        spacing: Theme.spacingM
        // A wheel notch pages one month: down or right to the next.
        WheelHandler {
            property real travel: 0
            onWheel: (event) => {
                travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
                var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                travel -= steps * 120
                if (steps !== 0)
                    calendar.step(-steps)
            }
        }
        // The time, large, and today's date under it.
        ColumnLayout {
            Layout.fillWidth: true; Layout.leftMargin: Theme.spacingS
            spacing: 0
            Text {
                objectName: "calendarTime"
                text: Qt.formatTime(calendar.now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontSizeDisplay; font.weight: Font.Light; font.family: Theme.fontFamily
            }
            Text {
                objectName: "calendarDate"
                text: Qt.formatDate(calendar.now, "dddd d MMMM yyyy")
                color: Theme.accent
                font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }
        // The month shown, and the arrows to page through the months.
        RowLayout {
            Layout.fillWidth: true; spacing: Theme.spacingXS
            FlatButton {
                id: title
                objectName: "calendarTitle"
                Layout.preferredHeight: Theme.rowHeight
                leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                onClicked: calendar.today()
                Accessible.name: "Go to today"
                contentItem: Text {
                    text: Qt.locale().monthName(calendar.month) + " " + calendar.year
                    color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                id: todayButton
                objectName: "calendarToday"
                Layout.preferredHeight: Theme.rowHeight
                leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
                enabled: calendar.month !== calendar.now.getMonth() || calendar.year !== calendar.now.getFullYear()
                onClicked: calendar.today()
                Accessible.name: "Go to today"
                contentItem: Text {
                    text: "Today"
                    color: todayButton.enabled ? Theme.accent : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
            FlatButton {
                objectName: "calendarPrevious"
                Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
                onClicked: calendar.step(-1)
                Accessible.name: "Previous month"
                contentItem: Item { Icon { anchors.centerIn: parent; name: "chevron-right"; rotation: -90; size: Theme.iconSize } }
            }
            FlatButton {
                objectName: "calendarNext"
                Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
                onClicked: calendar.step(1)
                Accessible.name: "Next month"
                contentItem: Item { Icon { anchors.centerIn: parent; name: "chevron-right"; rotation: 90; size: Theme.iconSize } }
            }
        }
        // Both size their cells only when their own size changes, which can happen
        // before the cells exist when the popup is made ahead of use; size them here.
        DayOfWeekRow {
            id: weekRow
            Layout.fillWidth: true; Layout.preferredHeight: Theme.headingHeight
            locale: Qt.locale()
            spacing: 0
            delegate: Text {
                required property string shortName
                width: weekRow.availableWidth / 7; height: weekRow.availableHeight
                text: shortName; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
        MonthGrid {
            id: monthGrid
            objectName: "monthGrid"
            Layout.fillWidth: true; Layout.preferredHeight: 6 * calendar.cellHeight
            month: calendar.month; year: calendar.year
            locale: Qt.locale()
            spacing: 0
            delegate: Item {
                id: dayCell
                required property var model
                width: monthGrid.availableWidth / 7; height: monthGrid.availableHeight / 6
                readonly property bool inMonth: model.month === monthGrid.month
                readonly property bool isToday: model.today && inMonth
                HoverHandler { id: dayHover }
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) - Theme.spacingXS; height: width; radius: width / 2
                    visible: dayCell.isToday || dayHover.hovered
                    color: dayCell.isToday ? (dayHover.hovered ? Theme.accentHover : Theme.accent) : Theme.hover
                }
                Text {
                    anchors.centerIn: parent
                    text: dayCell.model.day
                    color: dayCell.isToday ? Theme.textOnAccent : dayCell.inMonth ? Theme.text : Theme.textDisabled
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                    font.weight: dayCell.isToday ? Font.DemiBold : Font.Normal
                }
            }
        }
    }
}
