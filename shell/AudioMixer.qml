// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The volume control's popup: the default output's volume, then a slider for each application
// playing sound.
Rectangle {
    id: audioMixer
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "audioMixer"
    readonly property int rowHeight: 50
    readonly property string outputName: {
        var outputs = panel.audioSource.outputs
        for (var i = 0; i < outputs.length; ++i)
            if (outputs[i].name === panel.audioSource.output) return outputs[i].description
        return "No output"
    }
    visible: panel.audioPopup === "mixer"
    width: 340
    height: Math.min(panel.height - shell.panelExtent - 20,
                     30 + 2 * 26 + rowHeight + Math.max(1, streamList.count) * rowHeight + 10)
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 12
    border.color: Qt.lighter(shell.panelColor, 1.6)
    MouseArea { anchors.fill: parent }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 0
        Text {
            Layout.fillWidth: true; Layout.preferredHeight: 26
            text: audioMixer.outputName; elide: Text.ElideRight
            color: shell.textColor; font.pixelSize: shell.fontSize; font.weight: Font.DemiBold; font.family: panel.uiFont
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: audioMixer.rowHeight
            spacing: 8
            MuteButton {
                objectName: "audioMute"
                level: panel.audioSource.volume; muted: panel.audioSource.muted
                Accessible.name: muted ? "Unmute" : "Mute"
                onClicked: panel.audioSource.toggleMute()
            }
            AudioSlider {
                objectName: "audioVolumeSlider"
                Layout.fillWidth: true
                value: panel.audioSource.volume; muted: panel.audioSource.muted
                onMoved: panel.audioSource.setVolume(Math.round(value))
            }
            Text { text: panel.audioSource.volume + "%"; color: shell.textColor; font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
        }
        Text {
            Layout.fillWidth: true; Layout.preferredHeight: 26
            text: "Applications"; verticalAlignment: Text.AlignBottom
            color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont
        }
        ListView {
            id: streamList
            objectName: "audioStreams"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: panel.audioSource.streams
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: streamRow
                required property int streamId
                required property string name
                required property string icon
                required property int volume
                required property bool muted
                width: ListView.view.width; height: audioMixer.rowHeight
                spacing: 8
                Image { source: "image://icons/" + streamRow.icon; sourceSize: Qt.size(24, 24); Layout.preferredWidth: 24; Layout.preferredHeight: 24; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Text { Layout.fillWidth: true; text: streamRow.name; elide: Text.ElideRight; color: shell.textColor; font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont }
                    AudioSlider {
                        objectName: "audioStreamSlider"
                        Layout.fillWidth: true; Layout.preferredHeight: 24
                        value: streamRow.volume; muted: streamRow.muted
                        onMoved: panel.audioSource.setStreamVolume(streamRow.streamId, Math.round(value))
                    }
                }
                MuteButton {
                    level: streamRow.volume; muted: streamRow.muted
                    Accessible.name: (muted ? "Unmute " : "Mute ") + streamRow.name
                    onClicked: panel.audioSource.toggleStreamMute(streamRow.streamId)
                }
            }
            Text { anchors.centerIn: parent; visible: streamList.count === 0; text: "No applications are playing sound"; color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont }
        }
    }
}
