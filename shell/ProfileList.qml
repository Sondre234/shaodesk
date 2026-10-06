// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The profile button's popup: the appearance profiles, the one in use marked.
Rectangle {
    id: profileList
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "profileList"
    visible: panel.audioPopup === "profiles"
    width: 240; height: 12 + 30 + shell.profiles.length * 42
    x: Math.max(8, Math.min(panel.audioPopupX - width / 2, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: shell.panelColor; radius: 10
    border.color: Qt.lighter(shell.panelColor, 1.6)
    MouseArea { anchors.fill: parent }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 0
        Text {
            width: parent.width; height: 30; leftPadding: 10; verticalAlignment: Text.AlignVCenter
            text: "Appearance"; color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: panel.uiFont
        }
        Repeater {
            model: shell.profiles
            delegate: Button {
                id: profileItem
                required property string modelData
                readonly property bool current: modelData === shell.profile
                objectName: "profileItem"
                width: parent.width; height: 42
                text: modelData
                Accessible.name: modelData + (current ? ", in use" : "")
                onClicked: {
                    panel.audioPopup = ""
                    if (!current)
                        shell.pickProfile(modelData)
                }
                background: Rectangle { color: profileItem.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.leftMargin: 4; radius: 4; color: profileItem.current ? shell.accent : "transparent"; border.color: profileItem.current ? shell.accent : Qt.lighter(shell.panelColor, 2.2) }
                    Text { Layout.fillWidth: true; text: profileItem.modelData; elide: Text.ElideRight; color: profileItem.current ? shell.accent : shell.textColor; font.pixelSize: shell.fontSize; font.family: panel.uiFont }
                }
            }
        }
    }
}
