// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The on-screen display: a pill with an icon, a label and a level, fading away when its time is up.
Item {
    id: osd
    required property string outputName
    readonly property var model: shell.osd
    readonly property bool mine: model.active && model.output === outputName
    // True while it is visible, including the fade out; the view hides the surface after that.
    readonly property bool visibleNow: mine || pill.opacity > 0
    readonly property bool hasLevel: model.percent >= 0
    width: 320
    height: 64 + 2 * 8

    Rectangle {
        id: pill
        objectName: "osdPill"
        anchors.centerIn: parent
        width: 300; height: 56; radius: 28
        color: Theme.surface
        border.color: Theme.border
        opacity: osd.mine ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.duration(osd.mine ? 90 : 320); easing.type: Theme.easing } }
        // What it is about: the loudspeaker for the volume, as the bar draws it, a sun for the
        // brightness, the bell for notifications, else a mark for a message.
        Item {
            id: glyph
            x: 20; anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22
            readonly property string kind: osd.model.kind
            SpeakerIcon {
                visible: glyph.kind === "volume" || glyph.kind === "muted"
                size: parent.width
                level: osd.model.percent
                muted: glyph.kind === "muted"
            }
            Icon {
                visible: !(glyph.kind === "volume" || glyph.kind === "muted")
                size: parent.width
                name: glyph.kind === "brightness" ? "sun" : glyph.kind === "dnd" ? "bell-off"
                      : glyph.kind === "notifications" ? "bell" : "info"
            }
        }
        Text {
            id: label
            objectName: "osdText"
            anchors.verticalCenter: parent.verticalCenter
            x: 60
            width: osd.hasLevel ? 92 : parent.width - 80
            text: osd.model.text
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge; font.family: Theme.fontFamily
            textFormat: Text.PlainText; elide: Text.ElideRight
        }
        Rectangle {
            id: track
            objectName: "osdLevel"
            visible: osd.hasLevel
            x: 160; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 160 - 56; height: 6; radius: 3
            color: Theme.selected
            Rectangle {
                width: parent.width * Math.max(0, osd.model.percent) / 100; height: parent.height; radius: 3
                color: osd.model.kind === "muted" ? Theme.textMuted : Theme.accent
                Behavior on width { NumberAnimation { duration: Theme.duration(80) } }
            }
        }
        Text {
            visible: osd.hasLevel
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: 18
            text: osd.model.percent
            color: Theme.text; opacity: 0.8
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        }
    }
}
