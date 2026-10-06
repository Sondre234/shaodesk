// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The notification cards: a stack of them from a corner of the focused monitor, each sliding in
// and out. Clicking a card runs its default action or dismisses it; hovering one holds its timer.
Item {
    id: cards
    required property string outputName
    readonly property var center: shell.notifications
    // The gap between the cards and the screen's edges, and the room on their other sides, where
    // their shadows fall when there are any.
    readonly property int margin: Theme.spacingL
    readonly property int spread: Math.max(margin, Theme.shadowMargin)
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
    // How long ago a card came, as the history says it, kept current while cards are up.
    property date now: new Date()
    Timer { interval: 30000; repeat: true; running: cards.active; onTriggered: cards.now = new Date() }
    onActiveChanged: now = new Date()
    function ago(time) {
        var seconds = (now.getTime() - time.getTime()) / 1000
        if (seconds < 60) return "now"
        if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
        return Qt.formatTime(time, "HH:mm")
    }
    width: center.cardWidth + margin + spread
    height: active ? Math.max(1, settled + margin + spread) : 1

    ListView {
        id: list
        objectName: "notificationCards"
        // Taller than any stack, so every card has a delegate and contentHeight is the stack's
        // height; the surface, not the list, bounds what is drawn. Cards start at the edge of
        // the corner they stack from.
        readonly property int room: 4000
        x: center.left ? cards.margin : cards.spread
        y: center.bottom ? cards.height - cards.margin - room : cards.margin
        width: center.cardWidth
        height: room
        interactive: false
        spacing: Theme.spacingL
        // Newest nearest the screen edge the stack starts from.
        verticalLayoutDirection: center.bottom ? ListView.BottomToTop : ListView.TopToBottom
        model: cards.active ? center.cards : null
        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.duration(160) }
                NumberAnimation { property: "x"; from: center.left ? -center.cardWidth : center.cardWidth; duration: Theme.duration(220); easing.type: Theme.easing }
            }
        }
        remove: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.duration(160) }
                NumberAnimation { property: "x"; to: center.left ? -center.cardWidth : center.cardWidth; duration: Theme.duration(200); easing.type: Theme.easingExit }
            }
        }
        displaced: Transition { NumberAnimation { properties: "y"; duration: Theme.duration(180); easing.type: Theme.easing } }
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
            // The installed application's icon, "" when it is not one.
            readonly property string appIcon: {
                var known = desktopEntry.length > 0 ? desktopEntry : app.toLowerCase()
                return shell.appFor(known).length > 0 ? shell.iconFor(known) : ""
            }
            // The header's icon: the application's, else the notification's own.
            readonly property string headerIcon: appIcon.length > 0 ? appIcon : icon
            width: list.width
            height: card.height

            Item {
                id: card
                objectName: "notificationCard"
                width: parent.width
                height: content.implicitHeight + 24
                Loader {
                    anchors.fill: parent
                    active: Theme.effects
                    sourceComponent: RectangularShadow {
                        radius: Theme.radiusLarge
                        blur: Theme.shadowBlur
                        offset: Qt.vector2d(0, Theme.shadowOffset)
                        color: Theme.shadow
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusLarge
                    color: Theme.surface
                    border.color: entry.critical ? Theme.danger : Theme.border
                    border.width: entry.critical ? 2 : 1
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onContainsMouseChanged: cards.center.hold(entry.notificationId, containsMouse)
                    onClicked: cards.center.activate(entry.notificationId)
                    Component.onDestruction: cards.center.hold(entry.notificationId, false)
                }
                ColumnLayout {
                    id: content
                    x: 12; y: 12; width: parent.width - 24
                    spacing: Theme.spacingM
                    // Who it is from and when: the application's icon and name, and the time.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingS + Theme.spacingXS
                        Image {
                            visible: entry.headerIcon.length > 0
                            Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                            sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                            fillMode: Image.PreserveAspectFit
                            source: entry.headerIcon.length > 0 ? "image://icons/" + entry.headerIcon : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entry.app.length > 0 ? entry.app + " · " + cards.ago(entry.time) : cards.ago(entry.time)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                            textFormat: Text.PlainText; elide: Text.ElideRight
                        }
                        Text {
                            objectName: "notificationClose"
                            text: "×"
                            color: closeArea.containsMouse ? Theme.text : Theme.textMuted
                            font.pixelSize: 18
                            MouseArea {
                                id: closeArea
                                anchors.fill: parent; anchors.margins: -6
                                hoverEnabled: true
                                onClicked: cards.center.dismiss(entry.notificationId)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
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
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: entry.summary
                                color: Theme.text
                                font.pixelSize: Theme.fontSize + 1; font.bold: true; font.family: Theme.fontFamily
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: entry.body
                                color: Theme.text; opacity: 0.85
                                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                textFormat: Text.StyledText
                                linkColor: Theme.accent
                                wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                                onLinkActivated: (link) => cards.center.openLink(link)
                            }
                            Rectangle {
                                visible: entry.progress >= 0
                                Layout.fillWidth: true; Layout.preferredHeight: 6; Layout.topMargin: 3
                                radius: 3; color: Theme.selected
                                Rectangle { width: parent.width * Math.max(0, entry.progress) / 100; height: parent.height; radius: 3; color: Theme.accent }
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
                                        background: Rectangle { radius: Theme.radiusSmall; color: action.pressed ? Theme.pressed : action.hovered ? Theme.selected : Theme.hover }
                                        contentItem: Text {
                                            text: action.text; color: Theme.text
                                            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
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
}
