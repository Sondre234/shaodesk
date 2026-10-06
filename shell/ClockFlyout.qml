// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The clock's flyout, as on Windows 11: the month calendar on a card at the bar's right end, and
// the notifications on another beyond it, away from the bar. A card is left out while it has
// nothing to show: the notifications while the shell serves none, the calendar with
// shell.widgets.calendar off.
Item {
    id: flyout
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    anchors.fill: parent
    objectName: "clockFlyout"
    readonly property bool open: panel.audioPopup === "clock"
    readonly property bool showCalendar: shell.widgets.calendar
    readonly property bool showNotifications: shell.notifications.serving
    // The bar's right end, which the cards line up with.
    readonly property rect barEnd: panel.barAnchor(barItem.x + barItem.width, 0)

    CalendarPopup {
        id: calendar
        panel: flyout.panel
        open: flyout.open && flyout.showCalendar
        anchorRect: flyout.barEnd
    }
    NotificationHistory {
        panel: flyout.panel
        open: flyout.open && flyout.showNotifications
        implicitWidth: calendar.implicitWidth
        anchorRect: flyout.showCalendar ? Qt.rect(calendar.x, calendar.y, calendar.width, calendar.height)
                                        : flyout.barEnd
    }
}
