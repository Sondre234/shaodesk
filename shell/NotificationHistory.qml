// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock flyout's notification card (ClockFlyout.qml): every notification still kept, by
// application, the one with the newest first, under a heading with a do-not-disturb switch and a
// button that clears them all. A click on a notification runs its default action, and the cross
// that shows while the pointer is over it removes it.
PopupCard {
    id: history
    required property var panel
    readonly property var center: shell.notifications
    objectName: "notificationHistory"
    // Looking at the list is reading it, also when it was made open.
    onOpened: { now = new Date(); center.markAllRead() }
    Connections { target: history.center; function onUnreadChanged() { if (history.open) history.center.markAllRead() } }
    // Now, for how long ago each came, refreshed while it shows.
    property date now: new Date()
    Timer { interval: 30000; running: history.visible; repeat: true; onTriggered: history.now = new Date() }
    function ago(time) {
        var seconds = (now.getTime() - time.getTime()) / 1000
        if (seconds < 60) return "Now"
        if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
        if (now.toDateString() === time.toDateString()) return Qt.formatTime(time, "HH:mm")
        return Qt.formatDate(time, "d MMM") + " " + Qt.formatTime(time, "HH:mm")
    }
    // An icon for a notification or its application: its image, its own icon, or its
    // application's, else none.
    function iconSource(n) {
        if (n.hasImage) return "image://notify/" + n.notificationId + "/" + Number(n.time)
        if (n.icon.length > 0) return n.icon.charAt(0) === "/" ? "file://" + n.icon : "image://icons/" + n.icon
        var known = n.desktopEntry.length > 0 ? n.desktopEntry : n.app.toLowerCase()
        return shell.appFor(known).length > 0 ? "image://icons/" + shell.iconFor(known) : ""
    }
    readonly property real padding: Theme.spacingL
    implicitHeight: 2 * padding + heading.implicitHeight + Theme.spacingM +
                    (center.history.count > 0 ? list.contentHeight : empty.implicitHeight)
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    ColumnLayout {
        anchors.fill: parent; anchors.margins: history.padding
        spacing: Theme.spacingM
        RowLayout {
            id: heading
            Layout.fillWidth: true; Layout.preferredHeight: Theme.rowHeight
            Layout.leftMargin: Theme.spacingS
            spacing: Theme.spacingM
            Text {
                Layout.fillWidth: true
                text: "Notifications"; elide: Text.ElideRight
                color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Switch {
                id: dnd
                objectName: "dndSwitch"
                text: "Do not disturb"
                checked: history.center.dnd
                onToggled: history.center.dnd = checked
                spacing: Theme.spacingM
                indicator: Rectangle {
                    x: dnd.leftPadding; y: parent.height / 2 - height / 2
                    width: 2 * height; height: Theme.iconSize; radius: height / 2
                    color: dnd.checked ? Theme.accent : Theme.selected
                    Rectangle {
                        x: dnd.checked ? parent.width - width - Theme.spacingXS : Theme.spacingXS
                        y: Theme.spacingXS; width: parent.height - 2 * Theme.spacingXS; height: width; radius: width / 2
                        color: dnd.checked ? Theme.textOnAccent : Theme.text
                        Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    }
                }
                contentItem: Text {
                    leftPadding: dnd.indicator.width + dnd.spacing
                    text: dnd.text; color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
            FlatButton {
                id: clear
                objectName: "clearNotifications"
                Layout.preferredHeight: Theme.rowHeight - Theme.spacingS
                leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
                enabled: history.center.history.count > 0
                onClicked: history.center.clearHistory()
                Accessible.name: "Clear all notifications"
                contentItem: Text {
                    text: "Clear all"
                    color: clear.enabled ? Theme.accent : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        // Nothing kept: a quiet bell and a line saying so.
        Item {
            id: empty
            objectName: "notificationsEmpty"
            visible: history.center.history.count === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            implicitHeight: emptyColumn.implicitHeight + 2 * Theme.spacingXL
            Column {
                id: emptyColumn
                anchors.centerIn: parent
                spacing: Theme.spacingM
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "bell"; size: Theme.appIconSizeLarge; color: Theme.textDisabled
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "No new notifications"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                }
            }
        }
        ListView {
            id: list
            objectName: "notificationList"
            visible: history.center.history.count > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: Theme.spacingL
            boundsBehavior: Flickable.StopAtBounds
            model: history.visible ? history.center.history.groups : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            // An application's heading, its icon and name, and its notifications under it.
            delegate: Column {
                id: group
                required property var modelData
                readonly property var notifications: modelData.notifications
                width: ListView.view.width
                spacing: Theme.spacingS
                RowLayout {
                    width: parent.width; height: Theme.headingHeight
                    spacing: Theme.spacingM
                    Image {
                        readonly property string icon: history.iconSource(Object.assign({}, group.notifications[0], { hasImage: false }))
                        visible: icon.length > 0
                        Layout.leftMargin: Theme.spacingS
                        Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                        sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                        fillMode: Image.PreserveAspectFit; source: icon; cache: false
                    }
                    Text {
                        Layout.fillWidth: true
                        text: group.modelData.app; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    }
                }
                Repeater {
                    model: group.notifications
                    delegate: Rectangle {
                        id: row
                        objectName: "notificationRow"
                        required property var modelData
                        readonly property var n: modelData
                        width: group.width
                        height: rowContent.implicitHeight + 2 * Theme.spacingM
                        radius: Theme.radiusMedium
                        color: rowHover.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
                        border.color: n.urgency === 2 ? Theme.danger : "transparent"
                        HoverHandler { id: rowHover }
                        TapHandler { onTapped: history.center.activate(row.n.notificationId) }
                        ColumnLayout {
                            id: rowContent
                            x: Theme.spacingL; y: Theme.spacingM
                            width: parent.width - 2 * Theme.spacingL
                            spacing: Theme.spacingXS
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text {
                                    Layout.fillWidth: true
                                    text: row.n.summary; textFormat: Text.PlainText
                                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.Wrap
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                    font.weight: row.n.read ? Font.DemiBold : Font.Bold
                                }
                                Text {
                                    Layout.alignment: Qt.AlignTop
                                    visible: !rowHover.hovered
                                    text: history.ago(row.n.time)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                                }
                                // Shown in the time's place while the pointer is over it.
                                FlatButton {
                                    objectName: "removeNotification"
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: Theme.iconSize + Theme.spacingS
                                    Layout.preferredHeight: Theme.iconSize + Theme.spacingS
                                    Layout.topMargin: -Theme.spacingXS
                                    opacity: rowHover.hovered ? 1 : 0
                                    visible: opacity > 0
                                    Accessible.name: "Dismiss"
                                    onClicked: history.center.removeFromHistory(row.n.notificationId)
                                    contentItem: Item {
                                        Icon { anchors.centerIn: parent; name: "x"; size: Theme.iconSizeSmall; color: Theme.textMuted }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; visible: text.length > 0
                                text: row.n.body
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                                textFormat: Text.StyledText; linkColor: Theme.accent
                                wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
                                onLinkActivated: (link) => history.center.openLink(link)
                            }
                        }
                    }
                }
            }
        }
    }
}
