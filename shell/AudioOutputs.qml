// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The volume control's other popup: the outputs to play through, the one in use marked.
Rectangle {
    id: audioOutputs
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "audioOutputs"
    visible: panel.audioPopup === "outputs"
    width: 300; height: 12 + 30 + panel.audioSource.outputs.length * 42
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? panel.barBottom + 8 : panel.barTop - height - 8
    color: Theme.surface; radius: Theme.radiusMedium
    border.color: Theme.border
    MouseArea { anchors.fill: parent }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 0
        Text {
            width: parent.width; height: 30; leftPadding: 10; verticalAlignment: Text.AlignVCenter
            text: "Output"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Repeater {
            model: panel.audioSource.outputs
            delegate: FlatButton {
                id: outputItem
                required property var modelData
                readonly property bool current: modelData.name === panel.audioSource.output
                objectName: "audioOutputItem"
                width: parent.width; height: 42
                text: modelData.description
                Accessible.name: modelData.description
                onClicked: { panel.audioSource.setOutput(modelData.name); panel.audioPopup = "" }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.leftMargin: 4; radius: 4; color: outputItem.current ? Theme.accent : "transparent"; border.color: outputItem.current ? Theme.accent : Theme.textMuted }
                    Text { Layout.fillWidth: true; text: outputItem.modelData.description; elide: Text.ElideRight; color: outputItem.current ? Theme.accent : Theme.text; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
                }
            }
        }
    }
}
