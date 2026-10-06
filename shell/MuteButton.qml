// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// A mute toggle drawn as the speaker it silences.
FlatButton {
    id: mute
    property int level: 0
    property bool muted: false
    width: 32; height: 32
    Layout.preferredWidth: 32; Layout.preferredHeight: 32
    contentItem: Item { SpeakerIcon { anchors.centerIn: parent; level: mute.level; muted: mute.muted; color: mute.muted ? Theme.textMuted : Theme.text } }
}
