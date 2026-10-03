// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The notification history popover above (or below) the panel's bell: every notification still
// kept, newest first, with a do-not-disturb switch and a button that clears the list.
Rectangle {
    id: history
    required property var panel
    required property Item barItem
    readonly property var center: shell.notifications
    readonly property string uiFont: panel.uiFont
    objectName: "notificationHistory"
    parent: panel
    visible: panel.audioPopup === "notifications"
    // Looking at the list is reading it.
    onVisibleChanged: if (visible) center.markAllRead()
    Connections { target: history.center; function onUnreadChanged() { if (history.visible) history.center.markAllRead() } }
    function ago(time) {
        var seconds = (Date.now() - time.getTime()) / 1000
        if (seconds < 60) return "now"
        if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
        if (new Date().toDateString() === time.toDateString()) return Qt.formatTime(time, "HH:mm")
        return Qt.formatDate(time, "d MMM") + " " + Qt.formatTime(time, "HH:mm")
    }
    width: 380
    height: Math.min(460, 56 + Math.max(90, list.contentHeight) + 8)
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 12
    border.color: Qt.lighter(shell.panelColor, 1.6)
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 6
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 36; spacing: 8
            Text {
                Layout.fillWidth: true; leftPadding: 4
                text: "Notifications"
                color: shell.textColor; font.pixelSize: shell.fontSize + 2; font.bold: true; font.family: history.uiFont
            }
            Switch {
                id: dnd
                objectName: "dndSwitch"
                text: "Do not disturb"
                checked: history.center.dnd
                onToggled: history.center.dnd = checked
                indicator: Rectangle {
                    x: dnd.leftPadding; y: parent.height / 2 - height / 2
                    width: 34; height: 18; radius: 9
                    color: dnd.checked ? shell.accent : Qt.lighter(shell.panelColor, 1.8)
                    Rectangle { x: dnd.checked ? parent.width - width - 2 : 2; y: 2; width: 14; height: 14; radius: 7; color: shell.textColor }
                }
                contentItem: Text {
                    leftPadding: dnd.indicator.width + 8
                    text: dnd.text; color: shell.textColor; opacity: 0.85
                    font.pixelSize: shell.fontSize - 1; font.family: history.uiFont
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Button {
                id: clear
                objectName: "clearNotifications"
                text: "Clear"
                enabled: history.center.history.count > 0
                onClicked: history.center.clearHistory()
                padding: 6; leftPadding: 12; rightPadding: 12
                background: Rectangle { radius: 6; color: clear.hovered && clear.enabled ? Qt.lighter(shell.panelColor, 1.9) : Qt.lighter(shell.panelColor, 1.5) }
                contentItem: Text {
                    text: clear.text; color: shell.textColor; opacity: clear.enabled ? 1 : 0.4
                    font.pixelSize: shell.fontSize - 1; font.family: history.uiFont
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
        }
        Text {
            visible: history.center.history.count === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            text: "No notifications"
            color: shell.textColor; opacity: 0.55
            font.pixelSize: shell.fontSize; font.family: history.uiFont
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        ListView {
            id: list
            objectName: "notificationList"
            visible: history.center.history.count > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 6
            model: history.visible ? history.center.history : null
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                id: row
                required property int notificationId
                required property string app
                required property string icon
                required property string summary
                required property string body
                required property bool hasImage
                required property string desktopEntry
                required property int urgency
                required property bool read
                required property var time
                width: list.width - 8
                height: rowContent.implicitHeight + 16
                radius: 8
                color: rowArea.containsMouse ? Qt.lighter(shell.panelColor, 1.35) : Qt.lighter(shell.panelColor, 1.18)
                border.color: row.urgency === 2 ? "#ff6b6b" : "transparent"
                readonly property string iconSource: {
                    if (hasImage) return "image://notify/" + notificationId + "/" + Number(time)
                    if (icon.length > 0) return "image://icons/" + icon
                    var known = desktopEntry.length > 0 ? desktopEntry : app.toLowerCase()
                    return shell.appFor(known).length > 0 ? "image://icons/" + shell.iconFor(known) : ""
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: history.center.activate(row.notificationId)
                }
                RowLayout {
                    id: rowContent
                    x: 8; y: 8; width: parent.width - 16; spacing: 8
                    Image {
                        visible: row.iconSource.length > 0
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32
                        sourceSize: Qt.size(64, 64); fillMode: Image.PreserveAspectFit
                        source: row.iconSource; cache: false
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: row.app + " · " + history.ago(row.time)
                                color: shell.textColor; opacity: 0.6
                                font.pixelSize: Math.max(6, shell.fontSize - 2); font.family: history.uiFont
                                textFormat: Text.PlainText; elide: Text.ElideRight
                            }
                            Text {
                                objectName: "removeNotification"
                                text: "×"
                                color: shell.textColor; opacity: removeArea.containsMouse ? 1 : 0.55
                                font.pixelSize: 16
                                MouseArea {
                                    id: removeArea
                                    anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                                    onClicked: history.center.removeFromHistory(row.notificationId)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true; visible: text.length > 0
                            text: row.summary
                            color: shell.textColor; font.bold: !row.read
                            font.pixelSize: shell.fontSize; font.family: history.uiFont
                            textFormat: Text.PlainText; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; visible: text.length > 0
                            text: row.body
                            color: shell.textColor; opacity: 0.8
                            font.pixelSize: Math.max(6, shell.fontSize - 1); font.family: history.uiFont
                            textFormat: Text.StyledText; linkColor: shell.accent
                            wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                            onLinkActivated: (link) => history.center.openLink(link)
                        }
                    }
                }
            }
        }
    }
}
