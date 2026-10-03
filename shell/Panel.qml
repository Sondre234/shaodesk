// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ShaoDe

Item {
    id: root
    property bool launcherOpen: false
    // The context menu belongs to a task, a pinned application (pinMenuApp), or the bar itself
    // when barMenuOpen is set. A task's menu offers to pin the application it belongs to.
    property int taskMenuId: -1
    property string taskMenuApp: ""
    property var pinMenuApp: null
    property bool barMenuOpen: false
    property real contextMenuX: 0
    // The windows the taskbar shows; a stand-in model replaces it in tests.
    property var taskSource: shell.tasks
    // The sound server, replaced in tests too. Its popup is "mixer" (a slider per application)
    // or "outputs" (the output to play through), shown above the volume control.
    property var audioSource: shell.audio
    property string audioPopup: ""
    property real audioPopupX: 0
    property bool menuOpen: launcherOpen || taskMenuId >= 0 || pinMenuApp !== null || barMenuOpen || audioPopup !== ""
    // Hovering an application's stacked button lists its windows above it: those of the pinned
    // application groupSlot, or with the app id groupWindowApp outside the pinned slots.
    property bool groupOpen: false
    property string groupSlot: ""
    property string groupWindowApp: ""
    property string groupIcon: ""
    property real groupX: 0
    property Item groupPending: null
    // The list needs room too, but not the keyboard: it opens under a window being typed in.
    readonly property bool expanded: menuOpen || groupOpen
    onExpandedChanged: shellView.setExpanded(expanded, menuOpen)
    onMenuOpenChanged: {
        if (menuOpen) groupOpen = false
        shellView.setExpanded(expanded, menuOpen)
    }
    onLauncherOpenChanged: {
        if (launcherOpen) { taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; search.text = ""; search.forceActiveFocus() }
    }
    function closeMenus() { launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false }
    // A stacked button hovered for a moment, or at once while another's list is open, shows its
    // windows; leaving both it and the list hides them again.
    function hoverGroup(button, hovered) {
        if (hovered && button.stacked && !menuOpen) {
            groupPending = button
            groupHide.stop()
            if (groupOpen) showGroup(); else groupShow.restart()
        } else if (!hovered && button === groupPending) {
            // Entering the next button can come before leaving this one.
            groupShow.stop()
            if (groupOpen) groupHide.restart()
        }
    }
    function showGroup() {
        var button = groupPending
        if (!button || !button.hovered || !button.stacked || menuOpen)
            return
        groupSlot = button.groupSlot
        groupWindowApp = button.groupWindowApp
        groupIcon = button.iconName
        groupX = button.mapToItem(root, button.width / 2, 0).x
        groupOpen = true
    }
    Timer { id: groupShow; interval: 350; onTriggered: root.showGroup() }
    Timer {
        id: groupHide; interval: 300
        onTriggered: if (!groupHover.hovered && !(root.groupPending && root.groupPending.hovered)) root.groupOpen = false
    }
    // Opens on press, as a desktop context menu does: waiting for a tap lost a press held
    // past the long-press time or moved while held. The new menu opens before the old one
    // closes, so the surface does not collapse in between.
    function openContextMenu(item, x, taskId, app) {
        contextMenuX = item.mapToItem(root, x, 0).x
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
        launcherOpen = false; audioPopup = ""
    }
    // Opens (or, when it is already open, closes) one of the volume control's popups.
    function toggleAudioPopup(kind, item) {
        if (audioPopup === kind) { audioPopup = ""; return }
        audioPopupX = item.mapToItem(root, item.width / 2, 0).x
        audioPopup = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false
    }
    function pinAction(appId) {
        // Reading shell.pinned re-evaluates the menu when pins change. Pinning waits until
        // the click is handled: the change rebuilds the menu, destroying the clicked item.
        var pinned = shell.pinned.some(function(app) { return app.appId === appId })
        return pinned ? { text: "Unpin from taskbar", run: function() { Qt.callLater(function() { shell.unpin(appId) }) } }
                      : { text: "Pin to taskbar", run: function() { Qt.callLater(function() { shell.pin(appId) }) } }
    }
    // Popups open away from the screen edge the bar sits on.
    // Tiling is per monitor: this panel shows and toggles its own.
    readonly property bool tiling: {
        var state = shell.workspaces[outputName]
        return state && state.tiling !== undefined ? state.tiling : shell.tiling
    }
    readonly property bool onTop: shell.panelTop
    readonly property string uiFont: shell.fontFamily.length > 0 ? shell.fontFamily : Qt.application.font.family
    readonly property bool floating: shell.panelRadius > 0 || shell.panelMarginLeft > 0 ||
                                     shell.panelMarginRight > 0 || shell.panelMarginTop > 0 ||
                                     shell.panelMarginBottom > 0
    Keys.onEscapePressed: closeMenus()

    // A window on the taskbar: its icon, with its title beside it unless shell.iconsOnly, else
    // as a tooltip. A pinned slot shows its launcher's icon rather than the window's.
    // With shell.groupWindows the button stands for all its application's windows, `group`:
    // stacked when there are several, it cycles through them and lists them on hover.
    component TaskButton: Button {
        id: task
        required property int taskId
        required property string title
        required property string appId
        required property bool active
        required property bool minimized
        // Inline components do not see this file's ids, so each use passes the panel in.
        required property Item panel
        property string iconName: appId
        property TaskFilter group: null
        // Where the hover list finds the group's windows (see the panel's groupSlot).
        property string groupSlot: ""
        property string groupWindowApp: appId
        readonly property int windows: group ? group.count : 1
        readonly property bool stacked: windows > 1
        readonly property bool shownActive: group && group.count > 0 ? group.activeTask >= 0 : active
        readonly property bool shownMinimized: group && group.count > 0 ? group.minimized : minimized
        height: shell.panelHeight - 10
        // Hovering a stacked button lists its windows, whatever the platform thinks of hover.
        hoverEnabled: true
        // The icon sits above the activity line, fitting however short the bar is.
        topPadding: 2; bottomPadding: 6; leftPadding: 6; rightPadding: 6
        onClicked: {
            task.panel.closeMenus()
            shell.tasks.activate(stacked ? group.nextTask() : taskId)
        }
        onHoveredChanged: task.panel.hoverGroup(task, hovered)
        Accessible.name: stacked ? title + " and " + (windows - 1) + " more" : title
        // The panel's surface is only as tall as the bar, so an in-window tooltip would be
        // squeezed onto the icon and swallow its clicks; a popup window of its own sits above
        // the bar instead. Qt before 6.8 has no popup windows and draws it in the bar.
        ToolTip {
            id: tip
            visible: shell.iconsOnly && !task.stacked && task.hovered && !task.panel.expanded && !task.pressed
            delay: 500
            text: task.title
            y: task.panel.onTop ? task.height + 6 : -implicitHeight - 6
            Component.onCompleted: if ("popupType" in tip) tip.popupType = Popup.Window
        }
        background: Rectangle {
            radius: 6
            color: task.shownActive ? Qt.lighter(shell.panelColor, 1.7) : (task.hovered ? Qt.lighter(shell.panelColor, 1.4) : "transparent")
            // Several windows: a second button's edge peeks out behind this one.
            Rectangle {
                visible: task.stacked
                z: -1; x: 3; y: 2; width: parent.width; height: parent.height - 4; radius: 6
                color: "transparent"; border.width: 1
                border.color: Qt.lighter(shell.panelColor, task.shownActive ? 2.1 : 1.7)
            }
            Row {
                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                spacing: 3
                Repeater {
                    model: task.stacked ? 2 : 1
                    Rectangle {
                        width: task.shownActive ? (shell.iconsOnly ? 18 : 28) / (task.stacked ? 2 : 1) : (task.stacked ? 6 : 10)
                        height: 3; radius: 1; color: task.shownMinimized ? "#627084" : shell.accent
                    }
                }
            }
            Rectangle {
                objectName: "taskCount"
                visible: task.stacked
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 1
                width: Math.max(14, count.implicitWidth + 6); height: 14; radius: 7
                color: shell.accent
                Text { id: count; anchors.centerIn: parent; text: task.windows; color: shell.panelColor; font.pixelSize: 10; font.bold: true; font.family: task.panel.uiFont }
            }
        }
        contentItem: RowLayout {
            spacing: 6
            Item { Layout.fillWidth: shell.iconsOnly }
            Image {
                readonly property int size: Math.min(22, task.availableHeight)
                source: "image://icons/" + task.iconName; sourceSize: Qt.size(size, size)
                Layout.preferredWidth: size; Layout.preferredHeight: size
            }
            Text { visible: !shell.iconsOnly; text: task.title; color: shell.textColor; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: shell.fontSize; font.family: task.panel.uiFont }
            Item { Layout.fillWidth: shell.iconsOnly }
        }
        // Right-click opens the task's menu; middle-click closes its window.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.MiddleButton
            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    task.panel.openContextMenu(task, 0, task.taskId, task.appId)
            }
            onClicked: (mouse) => {
                if (mouse.button === Qt.MiddleButton) { task.panel.closeMenus(); shell.tasks.close(task.taskId) }
            }
        }
    }

    // A loudspeaker with a wave per third of the volume, or crossed out while muted.
    component SpeakerIcon: Canvas {
        id: speaker
        property int level: 0
        property bool muted: false
        property color color: shell.textColor
        width: 22; height: 18
        onLevelChanged: requestPaint()
        onMutedChanged: requestPaint()
        onColorChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = speaker.color; ctx.strokeStyle = speaker.color
            ctx.lineWidth = 1.8; ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(1.5, 6.5); ctx.lineTo(5, 6.5); ctx.lineTo(9.5, 2.5)
            ctx.lineTo(9.5, 15.5); ctx.lineTo(5, 11.5); ctx.lineTo(1.5, 11.5)
            ctx.closePath(); ctx.fill()
            if (speaker.muted || speaker.level === 0) {
                ctx.beginPath()
                ctx.moveTo(13.5, 6); ctx.lineTo(19.5, 12); ctx.moveTo(19.5, 6); ctx.lineTo(13.5, 12)
                ctx.stroke()
                return
            }
            var waves = speaker.level > 66 ? 3 : speaker.level > 33 ? 2 : 1
            for (var i = 0; i < waves; ++i) {
                ctx.beginPath()
                ctx.arc(9.5, 9, 4 + i * 3.8, -0.75, 0.75)
                ctx.stroke()
            }
        }
    }
    component AudioSlider: Slider {
        id: slider
        property bool muted: false
        from: 0; to: 100; stepSize: 1
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth; height: 4; radius: 2
            color: Qt.lighter(shell.panelColor, 1.8)
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: slider.muted ? "#627084" : shell.accent }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 14; height: 14; radius: 7
            color: slider.muted ? "#8a96a8" : (slider.pressed || slider.hovered ? Qt.lighter(shell.accent, 1.15) : shell.accent)
        }
    }
    // A mute toggle drawn as the speaker it silences.
    component MuteButton: Button {
        id: mute
        property int level: 0
        property bool muted: false
        width: 32; height: 32
        Layout.preferredWidth: 32; Layout.preferredHeight: 32
        background: Rectangle { radius: 6; color: mute.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent" }
        contentItem: Item { SpeakerIcon { anchors.centerIn: parent; level: mute.level; muted: mute.muted; color: mute.muted ? "#8a96a8" : shell.textColor } }
    }

    MouseArea {
        anchors.fill: parent
        visible: root.menuOpen
        onClicked: root.closeMenus()
    }

    // Left-clicking the volume control: the default output's volume, then each application's.
    Rectangle {
        id: audioMixer
        objectName: "audioMixer"
        readonly property int rowHeight: 50
        readonly property string outputName: {
            var outputs = root.audioSource.outputs
            for (var i = 0; i < outputs.length; ++i)
                if (outputs[i].name === root.audioSource.output) return outputs[i].description
            return "No output"
        }
        visible: root.audioPopup === "mixer"
        width: 340
        height: Math.min(root.height - shell.panelExtent - 20,
                         30 + 2 * 26 + rowHeight + Math.max(1, streamList.count) * rowHeight + 10)
        x: Math.max(8, Math.min(root.audioPopupX - width / 2, root.width - width - 8))
        anchors.bottom: root.onTop ? undefined : bar.top; anchors.bottomMargin: 8
        anchors.top: root.onTop ? bar.bottom : undefined; anchors.topMargin: 8
        color: shell.panelColor; radius: 12
        border.color: Qt.lighter(shell.panelColor, 1.6)
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 0
            Text {
                Layout.fillWidth: true; Layout.preferredHeight: 26
                text: audioMixer.outputName; elide: Text.ElideRight
                color: shell.textColor; font.pixelSize: shell.fontSize; font.weight: Font.DemiBold; font.family: root.uiFont
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: audioMixer.rowHeight
                spacing: 8
                MuteButton {
                    objectName: "audioMute"
                    level: root.audioSource.volume; muted: root.audioSource.muted
                    Accessible.name: muted ? "Unmute" : "Mute"
                    onClicked: root.audioSource.toggleMute()
                }
                AudioSlider {
                    objectName: "audioVolumeSlider"
                    Layout.fillWidth: true
                    value: root.audioSource.volume; muted: root.audioSource.muted
                    onMoved: root.audioSource.setVolume(Math.round(value))
                }
                Text { text: root.audioSource.volume + "%"; color: shell.textColor; font.pixelSize: shell.fontSize - 1; font.family: root.uiFont; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
            }
            Text {
                Layout.fillWidth: true; Layout.preferredHeight: 26
                text: "Applications"; verticalAlignment: Text.AlignBottom
                color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: root.uiFont
            }
            ListView {
                id: streamList
                objectName: "audioStreams"
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                model: root.audioSource.streams
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
                        Text { Layout.fillWidth: true; text: streamRow.name; elide: Text.ElideRight; color: shell.textColor; font.pixelSize: shell.fontSize - 1; font.family: root.uiFont }
                        AudioSlider {
                            objectName: "audioStreamSlider"
                            Layout.fillWidth: true; Layout.preferredHeight: 24
                            value: streamRow.volume; muted: streamRow.muted
                            onMoved: root.audioSource.setStreamVolume(streamRow.streamId, Math.round(value))
                        }
                    }
                    MuteButton {
                        level: streamRow.volume; muted: streamRow.muted
                        Accessible.name: (muted ? "Unmute " : "Mute ") + streamRow.name
                        onClicked: root.audioSource.toggleStreamMute(streamRow.streamId)
                    }
                }
                Text { anchors.centerIn: parent; visible: streamList.count === 0; text: "No applications are playing sound"; color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: root.uiFont }
            }
        }
    }

    // Right-clicking the volume control: the outputs to play through.
    Rectangle {
        id: audioOutputs
        objectName: "audioOutputs"
        visible: root.audioPopup === "outputs"
        width: 300; height: 12 + 30 + root.audioSource.outputs.length * 42
        x: Math.max(8, Math.min(root.audioPopupX - width / 2, root.width - width - 8))
        anchors.bottom: root.onTop ? undefined : bar.top; anchors.bottomMargin: 8
        anchors.top: root.onTop ? bar.bottom : undefined; anchors.topMargin: 8
        color: shell.panelColor; radius: 10
        border.color: Qt.lighter(shell.panelColor, 1.6)
        MouseArea { anchors.fill: parent }
        Column {
            anchors.fill: parent; anchors.margins: 6; spacing: 0
            Text {
                width: parent.width; height: 30; leftPadding: 10; verticalAlignment: Text.AlignVCenter
                text: "Output"; color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: root.uiFont
            }
            Repeater {
                model: root.audioSource.outputs
                delegate: Button {
                    id: outputItem
                    required property var modelData
                    readonly property bool current: modelData.name === root.audioSource.output
                    objectName: "audioOutputItem"
                    width: parent.width; height: 42
                    text: modelData.description
                    Accessible.name: modelData.description
                    onClicked: { root.audioSource.setOutput(modelData.name); root.audioPopup = "" }
                    background: Rectangle { color: outputItem.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 }
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.leftMargin: 4; radius: 4; color: outputItem.current ? shell.accent : "transparent"; border.color: outputItem.current ? shell.accent : Qt.lighter(shell.panelColor, 2.2) }
                        Text { Layout.fillWidth: true; text: outputItem.modelData.description; elide: Text.ElideRight; color: outputItem.current ? shell.accent : shell.textColor; font.pixelSize: shell.fontSize; font.family: root.uiFont }
                    }
                }
            }
        }
    }

    Rectangle {
        id: launcher
        visible: root.launcherOpen
        width: Math.min(460, root.width - 24)
        height: root.height - shell.panelExtent - 20
        anchors.left: parent.left
        anchors.leftMargin: 12 + shell.panelMarginLeft
        anchors.bottom: root.onTop ? undefined : bar.top
        anchors.top: root.onTop ? bar.bottom : undefined
        anchors.bottomMargin: 10
        anchors.topMargin: 10
        color: shell.panelColor
        border.color: Qt.lighter(shell.panelColor, 1.65)
        radius: 14
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Applications"; color: shell.textColor; font.pixelSize: 21; font.weight: Font.DemiBold; font.family: root.uiFont }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Refresh"
                    onClicked: shell.refreshApps()
                    palette.buttonText: shell.textColor
                    background: Rectangle { color: parent.hovered ? "#304058" : "transparent"; radius: 6 }
                }
            }
            TextField {
                id: search
                objectName: "applicationSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                placeholderText: "Search applications"
                placeholderTextColor: Qt.darker(shell.textColor, 1.5)
                color: shell.textColor
                selectByMouse: true
                leftPadding: 12
                font.pixelSize: 14; font.family: root.uiFont
                background: Rectangle {
                    radius: 7
                    color: Qt.darker(shell.panelColor, 1.2)
                    border.color: search.activeFocus ? shell.accent : Qt.lighter(shell.panelColor, 1.7)
                }
                onAccepted: {
                    if (applications.count > 0 && shell.launch(applications.model[0].appId)) root.closeMenus()
                }
                Keys.onEscapePressed: root.closeMenus()
            }
            ListView {
                id: applications
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 3
                model: shell.apps.filter(function(app) {
                    return (app.name + " " + app.appId).toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                })
                ScrollBar.vertical: ScrollBar {}
                delegate: Button {
                    required property var modelData
                    width: ListView.view.width - 10
                    height: 48
                    onClicked: { if (shell.launch(modelData.appId)) root.closeMenus() }
                    background: Rectangle { radius: 7; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                    contentItem: RowLayout {
                        spacing: 12
                        Image { source: "image://icons/" + modelData.icon; sourceSize: Qt.size(30, 30); Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                        Text { text: modelData.name; color: shell.textColor; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true; font.family: root.uiFont }
                        Text { visible: modelData.configured; text: "Pinned"; color: shell.accent; font.pixelSize: 10; font.family: root.uiFont }
                        // Installed applications pin and unpin here; shown while hovered or pinned.
                        Button {
                            id: pinToggle
                            objectName: "pinToggle"
                            visible: !modelData.configured && (modelData.pinned || parent.parent.hovered || hovered)
                            text: modelData.pinned ? "Unpin" : "Pin"
                            Accessible.name: (modelData.pinned ? "Unpin " : "Pin ") + modelData.name + (modelData.pinned ? " from" : " to") + " taskbar"
                            onClicked: modelData.pinned ? shell.unpin(modelData.appId) : shell.pin(modelData.appId)
                            Layout.preferredHeight: 26
                            font.pixelSize: 11; font.family: root.uiFont
                            palette.buttonText: modelData.pinned ? shell.accent : shell.textColor
                            background: Rectangle { radius: 5; color: pinToggle.hovered ? Qt.lighter(shell.panelColor, 1.9) : "transparent"; border.color: Qt.lighter(shell.panelColor, 1.9) }
                        }
                    }
                }
                Text { anchors.centerIn: parent; visible: applications.count === 0; text: "No matching applications"; color: shell.textColor; font.family: root.uiFont }
            }
            Text {
                Layout.fillWidth: true
                text: "shaoDe"
                color: Qt.darker(shell.textColor, 1.7)
                font.pixelSize: 11; font.family: root.uiFont
            }
        }
    }

    Rectangle {
        id: contextMenu
        objectName: "contextMenu"
        readonly property var actions: root.taskMenuId >= 0
            ? [{ text: "Maximize / restore", run: function(id) { shell.tasks.maximize(id) } },
               { text: "Minimize", run: function(id) { shell.tasks.minimize(id) } }]
              .concat(root.taskMenuApp ? [root.pinAction(root.taskMenuApp)] : [])
              .concat([{ text: "Close window", run: function(id) { shell.tasks.close(id) } }])
            : root.pinMenuApp !== null
            ? [{ text: "Open " + root.pinMenuApp.name, run: function() { shell.launch(root.pinMenuApp.appId) } }]
              .concat(root.pinMenuApp.configured ? [] : [root.pinAction(root.pinMenuApp.appId)])
            : [{ text: root.tiling ? "Turn tiling off" : "Turn tiling on", enabled: shell.tilingAvailable,
                 run: function() { shell.toggleTiling(outputName) } },
               { text: "Applications", run: function() { root.launcherOpen = true } },
               { text: "Show desktop", run: function() { shell.tasks.showDesktop() } }]
        visible: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen
        width: 220; height: 12 + actions.length * 44 + (actions.length - 1) * 2
        x: Math.max(8, Math.min(root.contextMenuX, root.width - width - 8))
        anchors.bottom: root.onTop ? undefined : bar.top; anchors.bottomMargin: 8
        anchors.top: root.onTop ? bar.bottom : undefined; anchors.topMargin: 8
        color: shell.panelColor; radius: 10
        border.color: Qt.lighter(shell.panelColor, 1.6)
        MouseArea { anchors.fill: parent }
        Column {
            anchors.fill: parent; anchors.margins: 6; spacing: 2
            Repeater {
                model: contextMenu.actions
                delegate: Button {
                    required property var modelData
                    objectName: "contextMenuItem"
                    width: parent.width; height: 44
                    text: modelData.text
                    enabled: modelData.enabled !== false
                    opacity: enabled ? 1 : 0.4
                    palette.buttonText: shell.textColor
                    background: Rectangle { color: parent.hovered ? Qt.lighter(shell.panelColor, 1.5) : "transparent"; radius: 6 }
                    // Run before closing, so opening the launcher keeps the surface expanded.
                    onClicked: {
                        modelData.run(root.taskMenuId)
                        root.taskMenuId = -1; root.pinMenuApp = null; root.barMenuOpen = false
                    }
                }
            }
        }
    }

    // The windows of the hovered stacked button: clicking one focuses it (or minimizes it when
    // focused already), the cross or a middle click closes it, and a right click opens its menu.
    Rectangle {
        id: groupList
        objectName: "groupList"
        readonly property int rowHeight: 40
        visible: root.groupOpen
        width: 280; height: 12 + groupWindows.count * rowHeight + Math.max(0, groupWindows.count - 1) * 2
        x: Math.max(8, Math.min(root.groupX - width / 2, root.width - width - 8))
        anchors.bottom: root.onTop ? undefined : bar.top; anchors.bottomMargin: 8
        anchors.top: root.onTop ? bar.bottom : undefined; anchors.topMargin: 8
        color: shell.panelColor; radius: 10
        border.color: Qt.lighter(shell.panelColor, 1.6)
        HoverHandler {
            id: groupHover
            onHoveredChanged: if (hovered) groupHide.stop(); else groupHide.restart()
        }
        TaskFilter {
            id: groupWindows
            controller: shell; sourceModel: root.taskSource
            app: root.groupSlot; windowApp: root.groupWindowApp
            // A window closing may leave nothing to choose between.
            onCountChanged: if (count < 2) root.groupOpen = false
        }
        Column {
            anchors.fill: parent; anchors.margins: 6; spacing: 2
            Repeater {
                model: root.groupOpen ? groupWindows : null
                delegate: Button {
                    id: groupWindow
                    required property int taskId
                    required property string title
                    required property string appId
                    required property bool active
                    required property bool minimized
                    objectName: "groupWindow"
                    width: parent.width; height: groupList.rowHeight
                    Accessible.name: title
                    // Closing the list destroys this row, so it goes last.
                    onClicked: { shell.tasks.activate(taskId); root.groupOpen = false }
                    background: Rectangle {
                        radius: 6
                        color: groupWindow.hovered ? Qt.lighter(shell.panelColor, 1.5) : (groupWindow.active ? Qt.lighter(shell.panelColor, 1.3) : "transparent")
                        Rectangle { visible: groupWindow.active; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1; color: shell.accent }
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Image {
                            Layout.leftMargin: 4
                            Layout.preferredWidth: 20; Layout.preferredHeight: 20
                            source: "image://icons/" + root.groupIcon; sourceSize: Qt.size(20, 20)
                            opacity: groupWindow.minimized ? 0.5 : 1
                        }
                        Text {
                            Layout.fillWidth: true
                            text: groupWindow.title; elide: Text.ElideRight
                            color: groupWindow.minimized ? Qt.darker(shell.textColor, 1.4) : shell.textColor
                            font.pixelSize: shell.fontSize; font.family: root.uiFont
                        }
                        Button {
                            id: closeWindow
                            objectName: "groupWindowClose"
                            visible: groupWindow.hovered || hovered
                            Layout.preferredWidth: 24; Layout.preferredHeight: 24
                            Accessible.name: "Close " + groupWindow.title
                            onClicked: shell.tasks.close(groupWindow.taskId)
                            background: Rectangle { radius: 5; color: closeWindow.hovered ? "#c4443c" : "transparent" }
                            contentItem: Text { text: "\u2715"; color: shell.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton | Qt.MiddleButton
                        onPressed: (mouse) => {
                            if (mouse.button === Qt.RightButton)
                                root.openContextMenu(groupWindow, 0, groupWindow.taskId, groupWindow.appId)
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.MiddleButton) shell.tasks.close(groupWindow.taskId)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: bar
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: shell.panelMarginLeft; anchors.rightMargin: shell.panelMarginRight
        anchors.bottom: root.onTop ? undefined : parent.bottom
        anchors.top: root.onTop ? parent.top : undefined
        anchors.bottomMargin: shell.panelMarginBottom; anchors.topMargin: shell.panelMarginTop
        height: shell.panelHeight
        color: shell.panelColor
        radius: shell.panelRadius
        // A floating bar gets an outline; a docked one a line along its inner edge.
        border.width: root.floating ? 1 : 0
        border.color: Qt.lighter(shell.panelColor, 1.65)
        Rectangle {
            visible: !root.floating
            y: root.onTop ? parent.height - 1 : 0
            width: parent.width; height: 1; color: Qt.lighter(shell.panelColor, 1.65)
        }
        // Right-clicking the bar anywhere but on a task opens the bar's own menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: (mouse) => root.openContextMenu(bar, mouse.x, -1)
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
            spacing: 6
            Button {
                id: start
                Layout.preferredWidth: 44; Layout.preferredHeight: bar.height - 10
                onClicked: root.launcherOpen = !root.launcherOpen
                Accessible.name: "Applications"
                background: Rectangle { radius: 7; color: start.hovered || root.launcherOpen ? Qt.lighter(shell.panelColor, 1.8) : "transparent" }
                contentItem: Item {
                    Grid {
                        anchors.centerIn: parent; columns: 2; spacing: 3
                        Repeater { model: 4; Rectangle { width: 9; height: 9; radius: 2; color: shell.accent } }
                    }
                }
            }
            // A pinned application's slot shows its launcher, or its windows while it has any.
            // Dragging either slides the slot along the bar, the slots it passes halfway over
            // making way; dropping it keeps it where it was dragged to.
            Repeater {
                id: pinnedSlots
                model: shell.pinned
                // The slot being dragged, the slot whose place it takes, and how far the slots
                // in between step aside.
                property int dragFrom: -1
                property int dragTo: -1
                property real dragStep: 0
                delegate: RowLayout {
                    id: pinnedSlot
                    required property var modelData
                    required property int index
                    property real dragX: 0
                    readonly property bool dragging: pinnedSlots.dragFrom === index
                    function drag(handler) {
                        dragX = handler.activeTranslation.x
                        var center = x + width / 2 + dragX, to = index
                        for (var i = 0; i < pinnedSlots.count; ++i) {
                            var other = pinnedSlots.itemAt(i)
                            if ((i > index && center >= other.x + other.width / 2) ||
                                (i < index && center <= other.x + other.width / 2 && to === index))
                                to = i
                        }
                        pinnedSlots.dragStep = width + parent.spacing
                        pinnedSlots.dragTo = to
                        pinnedSlots.dragFrom = index
                    }
                    function drop() {
                        var to = pinnedSlots.dragTo
                        dragX = 0
                        pinnedSlots.dragFrom = pinnedSlots.dragTo = -1
                        // Moving the pin rebuilds the slots, this one and its drag handler with them.
                        if (to >= 0 && to !== index) {
                            var app = modelData.appId, target = pinnedSlots.itemAt(to).modelData.appId
                            Qt.callLater(function() { shell.movePin(app, target) })
                        }
                    }
                    readonly property real shift: {
                        var from = pinnedSlots.dragFrom, to = pinnedSlots.dragTo
                        if (from < 0 || index === from)
                            return 0
                        if (from < to && index > from && index <= to)
                            return -pinnedSlots.dragStep
                        if (from > to && index >= to && index < from)
                            return pinnedSlots.dragStep
                        return 0
                    }
                    property real shiftX: shift
                    Behavior on shiftX { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    spacing: 4
                    z: dragging ? 1 : 0
                    transform: Translate { x: pinnedSlot.dragging ? pinnedSlot.dragX : pinnedSlot.shiftX }
                    Button {
                        id: pinnedButton
                        objectName: "pinned:" + pinnedSlot.modelData.appId
                        visible: pinnedTasks.count === 0
                        Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                        // Padded like a window's button, so the icon stays put when one opens.
                        topPadding: 2; bottomPadding: 6
                        onClicked: { if (shell.launch(pinnedSlot.modelData.appId)) root.closeMenus() }
                        Accessible.name: pinnedSlot.modelData.name
                        background: Rectangle { radius: 7; color: parent.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                        contentItem: Item {
                            Image {
                                readonly property int size: Math.min(22, parent.height)
                                anchors.centerIn: parent; width: size; height: size
                                source: "image://icons/" + pinnedSlot.modelData.icon; sourceSize: Qt.size(size, size)
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onPressed: root.openContextMenu(pinnedButton, 0, -1, pinnedSlot.modelData)
                        }
                        DragHandler {
                            target: null
                            yAxis.enabled: false
                            onActiveTranslationChanged: if (active) pinnedSlot.drag(this)
                            onActiveChanged: if (!active) pinnedSlot.drop()
                        }
                    }
                    // Grouped, the slot's first window stands for all of them.
                    TaskFilter { id: pinnedWindows; controller: shell; app: pinnedSlot.modelData.appId; sourceModel: root.taskSource }
                    Repeater {
                        model: TaskFilter { id: pinnedTasks; controller: shell; app: pinnedSlot.modelData.appId; sourceModel: root.taskSource; grouped: shell.groupWindows }
                        delegate: TaskButton {
                            objectName: "pinnedTask:" + pinnedSlot.modelData.appId
                            panel: root
                            iconName: pinnedSlot.modelData.icon
                            group: shell.groupWindows ? pinnedWindows : null
                            groupSlot: pinnedSlot.modelData.appId
                            groupWindowApp: ""
                            width: shell.iconsOnly ? 40 : 160
                            Layout.preferredWidth: width; Layout.preferredHeight: height
                            DragHandler {
                                target: null
                                yAxis.enabled: false
                                onActiveTranslationChanged: if (active) pinnedSlot.drag(this)
                                onActiveChanged: if (!active) pinnedSlot.drop()
                            }
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Qt.lighter(shell.panelColor, 1.8) }
            ListView {
                id: taskList
                objectName: "taskList"
                // As tall as a button, so its tasks line up with the pinned ones: a horizontal
                // list places each delegate at its top, whatever the delegate's own y.
                Layout.fillWidth: true; Layout.preferredHeight: shell.panelHeight - 10
                orientation: ListView.Horizontal; spacing: 4; clip: true
                // Dragging moves a single task, not the list; the wheel scrolls an overflowing one.
                interactive: false
                // The task being dragged, the task whose place it takes, and how far the tasks
                // in between step aside.
                property int dragFrom: -1
                property int dragTo: -1
                property real dragStep: 0
                model: TaskFilter { controller: shell; sourceModel: root.taskSource; grouped: shell.groupWindows }
                moveDisplaced: Transition { NumberAnimation { property: "x"; duration: 120; easing.type: Easing.OutCubic } }
                WheelHandler {
                    enabled: taskList.contentWidth > taskList.width
                    onWheel: (event) => {
                        var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        taskList.contentX = Math.max(0, Math.min(taskList.contentWidth - taskList.width,
                                                                 taskList.contentX - delta / 2))
                    }
                }
                delegate: TaskButton {
                    id: taskButton
                    required property int index
                    panel: root
                    // Windows without an app id have nothing to group by.
                    property TaskFilter appWindows: TaskFilter { controller: shell; sourceModel: root.taskSource; windowApp: taskButton.appId }
                    group: shell.groupWindows && appId !== "" ? appWindows : null
                    width: shell.iconsOnly ? 40 : Math.min(185, Math.max(92, taskList.width / Math.max(1, taskList.count) - 4))
                    z: reorder.active ? 1 : 0
                    readonly property real shift: {
                        var from = taskList.dragFrom, to = taskList.dragTo
                        if (from < 0 || index === from)
                            return 0
                        if (from < to && index > from && index <= to)
                            return -taskList.dragStep
                        if (from > to && index >= to && index < from)
                            return taskList.dragStep
                        return 0
                    }
                    property real shiftX: shift
                    Behavior on shiftX { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    transform: Translate { x: reorder.active ? reorder.activeTranslation.x : taskButton.shiftX }
                    // The task follows the pointer through the list, the tasks it passes halfway
                    // over making way, and moves on release; the press never becomes a click.
                    DragHandler {
                        id: reorder
                        target: null
                        yAxis.enabled: false
                        onActiveTranslationChanged: {
                            if (!active)
                                return
                            // Tasks are all as wide, so past a neighbour's middle is half a step.
                            var step = taskButton.width + taskList.spacing
                            var to = taskButton.index + Math.round(activeTranslation.x / step)
                            taskList.dragStep = step
                            taskList.dragTo = Math.max(0, Math.min(taskList.count - 1, to))
                            taskList.dragFrom = taskButton.index
                        }
                        onActiveChanged: {
                            if (active)
                                return
                            var from = taskList.dragFrom, to = taskList.dragTo
                            taskList.dragFrom = taskList.dragTo = -1
                            // The filter reports a move as a new layout, which rebuilds every
                            // task, this one and its drag handler with them.
                            // By then this task's context is gone, taking the list's id with it.
                            var list = taskList
                            if (from >= 0 && to >= 0 && to !== from)
                                Qt.callLater(function() {
                                    var contentX = list.contentX
                                    list.model.move(from, to, 1)
                                    list.contentX = contentX
                                })
                        }
                    }
                }
            }
            // This output's workspaces: the current one highlighted, a dot under those with
            // windows. Scrolling pages through them; clicking a number switches to it.
            Row {
                id: workspaceIndicator
                objectName: "workspaceIndicator"
                readonly property var workspaceState: shell.workspaces[outputName] || ({ current: 1, occupied: [] })
                function show(number) {
                    if (number >= 1 && number <= shell.workspaceCount && number !== workspaceState.current)
                        shell.showWorkspace(outputName, number)
                }
                visible: shell.workspaceCount > 1
                spacing: 2
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    model: shell.workspaceCount
                    delegate: Button {
                        id: workspaceButton
                        required property int index
                        readonly property int number: index + 1
                        readonly property bool current: workspaceIndicator.workspaceState.current === number
                        readonly property bool occupied: workspaceIndicator.workspaceState.occupied.indexOf(number) >= 0
                        objectName: "workspace" + number
                        width: 26; height: bar.height - 14
                        onClicked: { root.closeMenus(); workspaceIndicator.show(number) }
                        Accessible.name: "Workspace " + number
                        background: Rectangle {
                            radius: 6
                            color: workspaceButton.current ? Qt.lighter(shell.panelColor, 1.8) : (workspaceButton.hovered ? Qt.lighter(shell.panelColor, 1.4) : "transparent")
                        }
                        contentItem: Item {
                            Text {
                                anchors.centerIn: parent
                                text: workspaceButton.number
                                color: workspaceButton.current ? shell.accent : shell.textColor
                                font.pixelSize: shell.fontSize; font.family: root.uiFont
                                font.weight: workspaceButton.current ? Font.DemiBold : Font.Normal
                            }
                            Rectangle {
                                visible: workspaceButton.occupied
                                anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                                width: 4; height: 4; radius: 2
                                color: workspaceButton.current ? shell.accent : shell.textColor
                            }
                        }
                    }
                }
                // A wheel notch (or a touchpad's worth of travel) moves one workspace, stopping
                // at either end; down or right goes to the next.
                WheelHandler {
                    property real travel: 0
                    onWheel: (event) => {
                        travel += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                        travel -= steps * 120
                        if (steps !== 0) {
                            var target = workspaceIndicator.workspaceState.current - steps
                            workspaceIndicator.show(Math.max(1, Math.min(shell.workspaceCount, target)))
                        }
                    }
                }
            }
            Button {
                id: tilingToggle
                objectName: "tilingToggle"
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                enabled: shell.tilingAvailable
                opacity: enabled ? 1 : 0.4
                onClicked: { root.closeMenus(); shell.toggleTiling(outputName) }
                Accessible.name: root.tiling ? "Tiling on" : "Tiling off"
                background: Rectangle {
                    radius: 7
                    color: root.tiling ? Qt.lighter(shell.panelColor, 1.8) : (tilingToggle.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                // On: a dwindle split in the accent colour. Off: two overlapping windows.
                contentItem: Item {
                    Item {
                        anchors.centerIn: parent; width: 22; height: 16
                        visible: root.tiling
                        Rectangle { width: 10; height: 16; radius: 2; color: shell.accent }
                        Rectangle { x: 12; width: 10; height: 7; radius: 2; color: shell.accent }
                        Rectangle { x: 12; y: 9; width: 10; height: 7; radius: 2; color: shell.accent }
                    }
                    Item {
                        anchors.centerIn: parent; width: 22; height: 16
                        visible: !root.tiling
                        Rectangle { width: 15; height: 11; radius: 2; color: "transparent"; border.color: shell.textColor; border.width: 2 }
                        Rectangle { x: 7; y: 5; width: 15; height: 11; radius: 2; color: tilingToggle.hovered ? Qt.lighter(shell.panelColor, 1.55) : shell.panelColor; border.color: shell.textColor; border.width: 2 }
                    }
                }
            }
            // The default output's volume. Left-click: per-application volumes; right-click: the
            // output; wheel: louder or quieter; middle-click: mute.
            Button {
                id: audioWidget
                objectName: "audioWidget"
                visible: root.audioSource.available
                Layout.preferredWidth: 64; Layout.preferredHeight: bar.height - 10
                onClicked: root.toggleAudioPopup("mixer", audioWidget)
                Accessible.name: "Volume " + root.audioSource.volume + "%" + (root.audioSource.muted ? ", muted" : "")
                background: Rectangle {
                    radius: 7
                    color: root.audioPopup !== "" ? Qt.lighter(shell.panelColor, 1.8) : (audioWidget.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: 4
                        SpeakerIcon { anchors.verticalCenter: parent.verticalCenter; level: root.audioSource.volume; muted: root.audioSource.muted; color: root.audioSource.muted ? "#8a96a8" : shell.textColor }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.audioSource.volume + "%"
                            color: root.audioSource.muted ? "#8a96a8" : shell.textColor
                            font.pixelSize: Math.max(6, shell.fontSize - 1); font.family: root.uiFont
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            root.toggleAudioPopup("outputs", audioWidget)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) root.audioSource.toggleMute()
                    }
                }
                // Five percent a wheel notch, up for louder.
                WheelHandler {
                    property real travel: 0
                    onWheel: (event) => {
                        travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
                        var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                        travel -= steps * 120
                        if (steps !== 0)
                            root.audioSource.changeVolume(steps * 5)
                    }
                }
            }
            Text {
                id: clock
                property date now: new Date()
                text: Qt.formatTime(now, "HH:mm") + "\n" + Qt.formatDate(now, "ddd d MMM")
                color: shell.textColor; horizontalAlignment: Text.AlignRight
                font.pixelSize: Math.max(6, shell.fontSize - 1); font.family: root.uiFont
                Layout.preferredWidth: 82
                Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.now = new Date() }
            }
            Button {
                Layout.preferredWidth: 14; Layout.fillHeight: true
                onClicked: { root.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                background: Rectangle { color: parent.hovered ? shell.accent : Qt.lighter(shell.panelColor, 1.6); width: 3; anchors.right: parent.right }
            }
        }
        Rectangle {
            visible: shell.error.length > 0
            anchors.fill: parent; anchors.margins: 4
            color: "#542b32"; radius: 6
            Text { anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 10; text: shell.error; color: "#fff0f1"; elide: Text.ElideRight; font.pixelSize: 12 }
            Button { id: dismiss; anchors.right: parent.right; height: parent.height; width: 40; text: "×"; onClicked: shell.clearError() }
        }
    }
}
