// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock's popup: a month calendar with today marked, paged by month.
Rectangle {
    id: calendar
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "calendar"
    property int month: new Date().getMonth()
    property int year: new Date().getFullYear()
    function step(delta) {
        var d = new Date(year, month + delta, 1)
        year = d.getFullYear(); month = d.getMonth()
    }
    function today() { var d = new Date(); year = d.getFullYear(); month = d.getMonth() }
    visible: panel.audioPopup === "calendar"
    onVisibleChanged: if (visible) today()
    width: 288; height: 330
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 12
    border.color: Qt.lighter(shell.panelColor, 1.6)
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Button {
                objectName: "calendarPrevious"
                text: "\u2039"; Layout.preferredWidth: 32; Layout.preferredHeight: 30
                onClicked: calendar.step(-1)
                Accessible.name: "Previous month"
                background: Rectangle { radius: 6; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                contentItem: Text { text: parent.text; color: shell.textColor; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button {
                objectName: "calendarTitle"
                Layout.fillWidth: true; Layout.preferredHeight: 30
                onClicked: calendar.today()
                Accessible.name: "Go to today"
                background: Rectangle { radius: 6; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                contentItem: Text {
                    text: Qt.locale().monthName(calendar.month) + " " + calendar.year
                    color: shell.textColor; font.pixelSize: shell.fontSize + 1; font.weight: Font.DemiBold; font.family: panel.uiFont
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                objectName: "calendarNext"
                text: "\u203a"; Layout.preferredWidth: 32; Layout.preferredHeight: 30
                onClicked: calendar.step(1)
                Accessible.name: "Next month"
                background: Rectangle { radius: 6; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                contentItem: Text { text: parent.text; color: shell.textColor; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
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
                color: Qt.darker(shell.textColor, 1.5); font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont
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
                    color: shell.accent
                }
                Text {
                    anchors.centerIn: parent
                    text: dayCell.model.day
                    color: dayCell.model.today && dayCell.inMonth ? shell.panelColor : shell.textColor
                    opacity: dayCell.inMonth ? 1 : 0.35
                    font.pixelSize: shell.fontSize; font.family: panel.uiFont
                    font.weight: dayCell.model.today && dayCell.inMonth ? Font.DemiBold : Font.Normal
                }
            }
        }
    }
}
