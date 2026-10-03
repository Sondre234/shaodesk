// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The notification cards: a stack of them from a corner of the focused monitor, each sliding in
// and out. Clicking a card runs its default action or dismisses it; hovering one holds its timer.
Item {
    id: cards
    required property string outputName
    readonly property var center: shell.notifications
    readonly property int margin: 12
    readonly property string uiFont: shell.fontFamily.length > 0 ? shell.fontFamily : Qt.application.font.family
    // The output the cards are on; the surface stays while the last ones slide away.
    readonly property bool targeted: shell.cardsOutput === outputName
    property int settled: 0
    readonly property int shown: list.contentHeight
    readonly property bool active: targeted || settled > 0
    onShownChanged: {
        if (shown >= settled) { settled = shown; shrink.stop() }
        else shrink.restart()
    }
    // The surface follows the cards' height up at once but down only after they have left, or a
    // card sliding out would be cut off.
    Timer { id: shrink; interval: 320; onTriggered: cards.settled = cards.shown }
    width: center.cardWidth + 2 * margin
    height: active ? Math.max(1, settled + 2 * margin) : 1

    ListView {
        id: list
        objectName: "notificationCards"
        // Taller than any stack, so every card has a delegate and contentHeight is the stack's
        // height; the surface, not the list, bounds what is drawn. Cards start at the edge of
        // the corner they stack from.
        readonly property int room: 4000
        x: cards.margin
        y: center.bottom ? cards.height - cards.margin - room : cards.margin
        width: center.cardWidth
        height: room
        interactive: false
        spacing: 10
        // Newest nearest the screen edge the stack starts from.
        verticalLayoutDirection: center.bottom ? ListView.BottomToTop : ListView.TopToBottom
        model: cards.active ? center.cards : null
        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
                NumberAnimation { property: "x"; from: center.left ? -center.cardWidth : center.cardWidth; duration: 220; easing.type: Easing.OutCubic }
            }
        }
        remove: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: 160 }
                NumberAnimation { property: "x"; to: center.left ? -center.cardWidth : center.cardWidth; duration: 200; easing.type: Easing.InCubic }
            }
        }
        displaced: Transition { NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic } }
        delegate: Item {
            id: entry
            required property int index
            required property int notificationId
            required property string app
            required property string icon
            required property string summary
            required property string body
            required property var actions
            required property bool hasDefault
            required property int urgency
            required property int progress
            required property bool hasImage
            required property string desktopEntry
            required property var time
            readonly property bool critical: urgency === 2
            readonly property string iconSource: {
                if (hasImage) return "image://notify/" + notificationId + "/" + Number(time)
                if (icon.length > 0) return "image://icons/" + icon
                var known = desktopEntry.length > 0 ? desktopEntry : app.toLowerCase()
                return shell.appFor(known).length > 0 ? "image://icons/" + shell.iconFor(known) : ""
            }
            width: list.width
            height: card.height

            Rectangle {
                id: card
                objectName: "notificationCard"
                width: parent.width
                height: content.implicitHeight + 24
                radius: 12
                color: shell.panelColor
                border.color: entry.critical ? "#ff6b6b" : Qt.lighter(shell.panelColor, 1.6)
                border.width: entry.critical ? 2 : 1
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onContainsMouseChanged: cards.center.hold(entry.notificationId, containsMouse)
                    onClicked: cards.center.activate(entry.notificationId)
                    Component.onDestruction: cards.center.hold(entry.notificationId, false)
                }
                RowLayout {
                    id: content
                    x: 12; y: 12; width: parent.width - 24
                    spacing: 10
                    Image {
                        visible: entry.iconSource.length > 0
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 40; Layout.preferredHeight: 40
                        sourceSize: Qt.size(80, 80)
                        fillMode: Image.PreserveAspectFit
                        source: entry.iconSource
                        cache: false
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: entry.app
                                color: shell.textColor; opacity: 0.6
                                font.pixelSize: Math.max(6, shell.fontSize - 2); font.family: cards.uiFont
                                textFormat: Text.PlainText; elide: Text.ElideRight
                            }
                            Text {
                                objectName: "notificationClose"
                                text: "×"
                                color: shell.textColor; opacity: closeArea.containsMouse ? 1 : 0.55
                                font.pixelSize: 18
                                MouseArea {
                                    id: closeArea
                                    anchors.fill: parent; anchors.margins: -6
                                    hoverEnabled: true
                                    onClicked: cards.center.dismiss(entry.notificationId)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: entry.summary
                            color: shell.textColor
                            font.pixelSize: shell.fontSize + 1; font.bold: true; font.family: cards.uiFont
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: entry.body
                            color: shell.textColor; opacity: 0.85
                            font.pixelSize: shell.fontSize; font.family: cards.uiFont
                            textFormat: Text.StyledText
                            linkColor: shell.accent
                            wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                            onLinkActivated: (link) => cards.center.openLink(link)
                        }
                        Rectangle {
                            visible: entry.progress >= 0
                            Layout.fillWidth: true; Layout.preferredHeight: 6; Layout.topMargin: 3
                            radius: 3; color: Qt.lighter(shell.panelColor, 1.8)
                            Rectangle { width: parent.width * Math.max(0, entry.progress) / 100; height: parent.height; radius: 3; color: shell.accent }
                        }
                        Flow {
                            visible: entry.actions.length > 0
                            Layout.fillWidth: true; Layout.topMargin: 4
                            spacing: 6
                            Repeater {
                                model: entry.actions
                                delegate: Button {
                                    id: action
                                    required property var modelData
                                    objectName: "notificationAction"
                                    text: modelData.label
                                    padding: 6; leftPadding: 12; rightPadding: 12
                                    onClicked: cards.center.invoke(entry.notificationId, modelData.key)
                                    background: Rectangle { radius: 6; color: action.hovered ? Qt.lighter(shell.panelColor, 1.9) : Qt.lighter(shell.panelColor, 1.5) }
                                    contentItem: Text {
                                        text: action.text; color: shell.textColor
                                        font.pixelSize: shell.fontSize; font.family: cards.uiFont
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
