// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A mute toggle drawn as the speaker it silences.
Button {
    id: mute
    property int level: 0
    property bool muted: false
    width: 32; height: 32
    Layout.preferredWidth: 32; Layout.preferredHeight: 32
    background: Rectangle { radius: 6; color: mute.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent" }
    contentItem: Item { SpeakerIcon { anchors.centerIn: parent; level: mute.level; muted: mute.muted; color: mute.muted ? "#8a96a8" : shell.textColor } }
}
