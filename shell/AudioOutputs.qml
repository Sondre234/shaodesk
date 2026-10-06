// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The volume control's other popup: the outputs to play through, the one in use marked.
Rectangle {
    id: audioOutputs
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "audioOutputs"
    visible: panel.audioPopup === "outputs"
    width: 300; height: 12 + 30 + panel.audioSource.outputs.length * 42
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 10
    border.color: Qt.lighter(shell.panelColor, 1.6)
    MouseArea { anchors.fill: parent }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 0
        Text {
            width: parent.width; height: 30; leftPadding: 10; verticalAlignment: Text.AlignVCenter
            text: "Output"; color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont
        }
        Repeater {
            model: panel.audioSource.outputs
            delegate: Button {
                id: outputItem
                required property var modelData
                readonly property bool current: modelData.name === panel.audioSource.output
                objectName: "audioOutputItem"
                width: parent.width; height: 42
                text: modelData.description
                Accessible.name: modelData.description
                onClicked: { panel.audioSource.setOutput(modelData.name); panel.audioPopup = "" }
                background: Rectangle { color: outputItem.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.leftMargin: 4; radius: 4; color: outputItem.current ? shell.accent : "transparent"; border.color: outputItem.current ? shell.accent : Qt.lighter(shell.panelColor, 2.2) }
                    Text { Layout.fillWidth: true; text: outputItem.modelData.description; elide: Text.ElideRight; color: outputItem.current ? shell.accent : shell.textColor; font.pixelSize: shell.fontSize; font.family: panel.uiFont }
                }
            }
        }
    }
}
