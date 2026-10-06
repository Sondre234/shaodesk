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
        Canvas {
            id: glyph
            x: 20; anchors.verticalCenter: parent.verticalCenter
            width: 26; height: 22
            readonly property string kind: osd.model.kind
            readonly property int level: osd.model.percent
            onKindChanged: requestPaint()
            onLevelChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Theme.text; ctx.strokeStyle = Theme.text
                ctx.lineWidth = 2; ctx.lineCap = "round"
                if (kind === "volume" || kind === "muted") {
                    ctx.beginPath()
                    ctx.moveTo(2, 8); ctx.lineTo(6, 8); ctx.lineTo(11, 3); ctx.lineTo(11, 19); ctx.lineTo(6, 14); ctx.lineTo(2, 14)
                    ctx.closePath(); ctx.fill()
                    if (kind === "muted" || level === 0) {
                        ctx.beginPath(); ctx.moveTo(16, 7); ctx.lineTo(23, 15); ctx.moveTo(23, 7); ctx.lineTo(16, 15); ctx.stroke()
                    } else {
                        var waves = level > 66 ? 3 : level > 33 ? 2 : 1
                        for (var i = 0; i < waves; ++i) { ctx.beginPath(); ctx.arc(11, 11, 5 + i * 4.2, -0.75, 0.75); ctx.stroke() }
                    }
                } else if (kind === "brightness") {
                    ctx.beginPath(); ctx.arc(13, 11, 4.5, 0, Math.PI * 2); ctx.fill()
                    for (var r = 0; r < 8; ++r) {
                        var a = r * Math.PI / 4
                        ctx.beginPath(); ctx.moveTo(13 + Math.cos(a) * 7.5, 11 + Math.sin(a) * 7.5)
                        ctx.lineTo(13 + Math.cos(a) * 10, 11 + Math.sin(a) * 10); ctx.stroke()
                    }
                } else if (kind === "dnd") {
                    ctx.beginPath(); ctx.arc(13, 11, 9, 0, Math.PI * 2); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(7, 11); ctx.lineTo(19, 11); ctx.stroke()
                } else {
                    ctx.beginPath(); ctx.arc(13, 11, 4, 0, Math.PI * 2); ctx.fill()
                }
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
