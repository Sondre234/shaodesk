// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock flyout's calendar card (ClockFlyout.qml): a month calendar with today marked, paged
// by month.
PopupCard {
    id: calendar
    required property var panel
    objectName: "calendar"
    property int month: new Date().getMonth()
    property int year: new Date().getFullYear()
    function step(delta) {
        var d = new Date(year, month + delta, 1)
        year = d.getFullYear(); month = d.getMonth()
    }
    function today() { var d = new Date(); year = d.getFullYear(); month = d.getMonth() }
    onOpened: today()
    implicitWidth: 288; implicitHeight: 330
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            FlatButton {
                objectName: "calendarPrevious"
                text: "\u2039"; Layout.preferredWidth: 32; Layout.preferredHeight: 30
                onClicked: calendar.step(-1)
                Accessible.name: "Previous month"
                contentItem: Text { text: parent.text; color: Theme.text; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            FlatButton {
                objectName: "calendarTitle"
                Layout.fillWidth: true; Layout.preferredHeight: 30
                onClicked: calendar.today()
                Accessible.name: "Go to today"
                contentItem: Text {
                    text: Qt.locale().monthName(calendar.month) + " " + calendar.year
                    color: Theme.text; font.pixelSize: Theme.fontSize + 1; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
            FlatButton {
                objectName: "calendarNext"
                text: "\u203a"; Layout.preferredWidth: 32; Layout.preferredHeight: 30
                onClicked: calendar.step(1)
                Accessible.name: "Next month"
                contentItem: Text { text: parent.text; color: Theme.text; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
        // Both size their cells only when their own size changes, which can happen
        // before the cells exist when the popup is made ahead of use; size them here.
        DayOfWeekRow {
            id: weekRow
            Layout.fillWidth: true; Layout.preferredHeight: 24
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
            Layout.fillWidth: true; Layout.fillHeight: true
            month: calendar.month; year: calendar.year
            locale: Qt.locale()
            spacing: 0
            delegate: Item {
                id: dayCell
                required property var model
                width: monthGrid.availableWidth / 7; height: monthGrid.availableHeight / 6
                readonly property bool inMonth: model.month === monthGrid.month
                Rectangle {
                    anchors.centerIn: parent; width: Math.min(parent.width, parent.height) - 2; height: width; radius: width / 2
                    visible: dayCell.model.today && dayCell.inMonth
                    color: Theme.accent
                }
                Text {
                    anchors.centerIn: parent
                    text: dayCell.model.day
                    color: dayCell.model.today && dayCell.inMonth ? Theme.textOnAccent : Theme.text
                    opacity: dayCell.inMonth ? 1 : 0.35
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                    font.weight: dayCell.model.today && dayCell.inMonth ? Font.DemiBold : Font.Normal
                }
            }
        }
    }
}
