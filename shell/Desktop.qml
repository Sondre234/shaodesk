// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: desktop
    color: shell.background
    Image {
        id: wallpaper
        anchors.fill: parent
        source: shell.wallpaper
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // A failed load is never retried on its own, and a reload keeps the same source, so
        // retry with backoff and reload the file whenever the configuration changes.
        function load() {
            source = ""
            source = Qt.binding(function() { return shell.wallpaper })
        }
        onStatusChanged: {
            if (status === Image.Error) {
                console.warn("shaoDe wallpaper failed to load, retrying in "
                             + retry.interval / 1000 + " s: " + source)
                retry.start()
            } else if (status === Image.Ready) {
                retry.interval = 1000
                shown = source
                if (previous.source.toString() !== "") fade.restart()
            }
        }
        Timer {
            id: retry
            interval: 1000
            onTriggered: {
                interval = Math.min(interval * 2, 60000)
                wallpaper.load()
            }
        }
        // A new wallpaper fades in over the one shown before (held by `previous`) once it has
        // loaded, rather than the background showing while it decodes.
        property url shown: ""
        onSourceChanged: {
            if (source.toString() !== "" && shown.toString() !== "" && source.toString() !== shown.toString()) {
                fade.stop()
                previous.source = shown
                previous.opacity = 1
            }
        }
        Connections {
            target: shell
            function onConfigChanged() {
                retry.stop()
                retry.interval = 1000
                wallpaper.load()
            }
        }
    }
    Image {
        id: previous
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        visible: source.toString() !== ""
        NumberAnimation {
            id: fade
            target: previous; property: "opacity"; to: 0; duration: 450; easing.type: Easing.InOutQuad
            onFinished: previous.source = ""
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: shell.wallpaper.toString().length === 0 || wallpaper.status === Image.Error
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.lighter(shell.background, 1.45) }
            GradientStop { position: 1; color: shell.background }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            menu.visible = mouse.button === Qt.RightButton
            menu.x = Math.max(0, Math.min(mouse.x, desktop.width - menu.width - 8))
            menu.y = Math.max(shell.panelTop ? shell.panelExtent : 0, Math.min(mouse.y, desktop.height - (shell.panelTop ? 0 : shell.panelExtent) - menu.height - 8))
        }
    }
    Column {
        x: 16; y: 24; spacing: 12
        Repeater {
            // Configured launchers only; applications pinned from the taskbar stay there.
            model: shell.pinned.filter(function(app) { return app.configured })
            delegate: Button {
                required property var modelData
                width: 88; height: 84
                onDoubleClicked: shell.launch(modelData.appId)
                Accessible.name: "Double-click to open " + modelData.name
                background: Rectangle { radius: 8; color: parent.hovered ? "#284b638a" : "transparent"; border.color: parent.hovered ? "#557da8ff" : "transparent" }
                contentItem: Column {
                    spacing: 6
                    Image { anchors.horizontalCenter: parent.horizontalCenter; width: 40; height: 40; sourceSize: Qt.size(40, 40); source: "image://icons/" + modelData.icon }
                    Text { width: parent.width; text: modelData.name; textFormat: Text.PlainText; color: shell.textColor; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                }
            }
        }
    }
    Rectangle {
        id: menu
        visible: false
        width: 210; height: 112; radius: 8
        color: shell.panelColor; border.color: Qt.lighter(shell.panelColor, 1.6)
        Column {
            anchors.fill: parent; anchors.margins: 6
            Button { width: parent.width; height: 48; text: "Refresh applications"; onClicked: { shell.refreshApps(); menu.visible = false } palette.buttonText: shell.textColor; background: Rectangle { color: parent.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 } }
            Button { width: parent.width; height: 48; text: "Show desktop"; onClicked: { shell.tasks.showDesktop(); menu.visible = false } palette.buttonText: shell.textColor; background: Rectangle { color: parent.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 } }
        }
    }
}
