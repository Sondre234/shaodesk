// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Shaodesk

Item {
    id: root
    // Set by the view: itself, and the compositor's name for the output it is on.
    required property var shellView
    required property string outputName
    property bool launcherOpen: false
    // The power menu, over the launcher by the power button in its bottom-right corner.
    property bool powerOpen: false
    // The popups are made when first needed, or a moment after startup so that the first use
    // finds them ready: what the bar shows first does not wait for them.
    property bool warm: false
    Timer { interval: 1500; running: true; onTriggered: root.warm = true }
    // The context menu belongs to a task, a pinned application (pinMenuApp), or the bar itself
    // when barMenuOpen is set. A task's menu offers to pin the application it belongs to.
    property int taskMenuId: -1
    property string taskMenuApp: ""
    property var pinMenuApp: null
    property bool barMenuOpen: false
    // The bar menu shows the appearance profiles instead of its own entries.
    property bool profileMenu: false
    property real contextMenuX: 0
    // The windows the taskbar shows; a stand-in model replaces it in tests.
    property var taskSource: shell.tasks
    // The sound server, replaced in tests too. Its popup is "mixer" (a slider per application)
    // or "outputs" (the output to play through), shown above the volume control.
    property var audioSource: shell.audio
    // Battery and network state; tests swap in one that reads a fake sysfs.
    property var statusSource: shell.status
    property string audioPopup: ""
    property real audioPopupX: 0
    // A tray item's menu: the item it belongs to ("" while closed), the entry whose children it
    // lists (0 for the top), and the entries passed through to get there, to go back to.
    property string trayMenuKey: ""
    property int trayMenuParent: 0
    property var trayMenuTrail: []
    property real trayMenuX: 0
    // Bumped when the open menu's item changes its entries, so they are read again.
    property int trayMenuRevision: 0
    property bool menuOpen: launcherOpen || taskMenuId >= 0 || pinMenuApp !== null || barMenuOpen || audioPopup !== "" ||
                            trayMenuKey !== ""
    // Hovering an application's stacked button lists its windows above it: those of the pinned
    // application groupSlot, or with the app id groupWindowApp outside the pinned slots.
    property bool groupOpen: false
    readonly property bool groupListHovered: groupListLoader.item ? groupListLoader.item.hovered : false
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
        if (launcherOpen) { taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = "" }
        else powerOpen = false
    }
    function closeMenus() { launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; trayMenuKey = "" }
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
        if (button && button.hovered && button.stacked && !menuOpen)
            openGroup(button)
    }
    function openGroup(button) {
        groupSlot = button.groupSlot
        groupWindowApp = button.groupWindowApp
        groupIcon = button.iconName
        groupX = button.mapToItem(root, button.width / 2, 0).x
        groupOpen = true
    }
    Timer { id: groupShow; interval: 350; onTriggered: root.showGroup() }
    Timer {
        id: groupHide; interval: 300
        onTriggered: if (!root.groupListHovered && !(root.groupPending && root.groupPending.hovered)) root.groupOpen = false
    }
    // Opens on press, as a desktop context menu does: waiting for a tap lost a press held
    // past the long-press time or moved while held. The new menu opens before the old one
    // closes, so the surface does not collapse in between.
    function openContextMenu(item, x, taskId, app) {
        contextMenuX = item.mapToItem(root, x, 0).x
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
        profileMenu = false
        launcherOpen = false; audioPopup = ""; trayMenuKey = ""
    }
    // Opens (or, when it is already open, closes) one of the volume control's popups.
    function toggleAudioPopup(kind, item) {
        if (audioPopup === kind) { audioPopup = ""; return }
        audioPopupX = item.mapToItem(root, item.width / 2, 0).x
        audioPopup = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; trayMenuKey = ""
    }
    // The power menu, from the power_menu action: the launcher opens with it, and closes with it
    // when asked again.
    function togglePowerMenu() {
        if (powerOpen) {
            closeMenus()
        } else if (shell.power.entries.length > 0) {
            launcherOpen = true
            powerOpen = true
        }
    }
    // For `shaodesk-shell --preview-popup NAME`, which shows one popup for a screenshot: opens it
    // the way its button would. "bar" opens none. Returns false for a name it does not know, or
    // when what the popup belongs to is not on the bar.
    function previewPopup(name) {
        taskList.forceLayout()
        var i
        switch (name) {
        case "bar":
            return true
        case "launcher":
            launcherOpen = true
            return true
        case "power":
            togglePowerMenu()
            return powerOpen
        case "bar-menu":
        case "profile-menu":
            openContextMenu(bar, bar.width / 2, -1)
            profileMenu = name === "profile-menu"
            return true
        case "task-menu":
            var task = taskList.itemAtIndex(0)
            if (task)
                openContextMenu(task, 0, task.taskId, task.appId)
            return task !== null
        case "pin-menu":
            var slot = pinnedSlots.itemAt(0)
            if (slot)
                openContextMenu(slot, 0, -1, slot.modelData)
            return slot !== null
        case "group":
            // A stacked button, in the task list or in a pinned slot.
            var buttons = []
            for (i = 0; i < taskList.count; ++i)
                buttons.push(taskList.itemAtIndex(i))
            for (i = 0; i < pinnedSlots.count; ++i)
                buttons = buttons.concat(pinnedSlots.itemAt(i).children)
            for (i = 0; i < buttons.length; ++i)
                if (buttons[i] && buttons[i].stacked) {
                    openGroup(buttons[i])
                    return true
                }
            return false
        case "tray-menu":
            for (i = 0; i < tray.children.length; ++i)
                if (tray.children[i].hasMenu) {
                    trayMenu(tray.children[i])
                    return true
                }
            return false
        case "calendar":
            toggleAudioPopup("calendar", clockButton)
            return clockButton.visible
        case "mixer":
        case "outputs":
            toggleAudioPopup(name, audioWidget)
            return audioWidget.visible
        case "profiles":
            toggleAudioPopup("profiles", profilesButton)
            return profilesButton.visible
        case "wallpapers":
            toggleAudioPopup("wallpapers", wallpapersButton)
            return wallpapersButton.visible
        case "notifications":
            toggleAudioPopup("notifications", bell)
            return bell.visible
        }
        return false
    }
    // Where a tray item's icon is on the screen, which some applications place a window by: the
    // panel spans its output's width, at its top or bottom edge.
    function trayPoint(item) {
        var p = item.mapToItem(root, item.width / 2, item.height / 2)
        return Qt.point(Math.round(Screen.virtualX + p.x),
                        Math.round(Screen.virtualY + (onTop ? p.y : Screen.height - height + p.y)))
    }
    // An item that is only a menu opens it; one that cannot be activated opens it after all.
    property Item trayActivating: null
    function trayActivate(button) {
        if (button.itemIsMenu) {
            trayMenu(button)
            return
        }
        closeMenus()
        var p = trayPoint(button)
        trayActivating = button
        shell.tray.activate(button.key, p.x, p.y)
    }
    function traySecondary(button) {
        closeMenus()
        var p = trayPoint(button)
        shell.tray.secondaryActivate(button.key, p.x, p.y)
    }
    // Opens the item's menu by it, as the bar's own menus open: on press, the new menu before
    // the old one closes. A second press closes it. An item without a menu (to the panel) is
    // asked to show its own.
    function trayMenu(button) {
        if (!button.hasMenu) {
            closeMenus()
            var p = trayPoint(button)
            shell.tray.contextMenu(button.key, p.x, p.y)
            return
        }
        if (trayMenuKey === button.key) {
            closeMenus()
            return
        }
        trayMenuX = button.mapToItem(root, button.width / 2, 0).x
        trayMenuTrail = []
        trayMenuParent = 0
        trayMenuKey = button.key
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false
    }
    // An entry picked: a submenu shows its entries in place, "Back" returns, anything else is
    // the application's to carry out, and the menu closes.
    function trayMenuPick(entry) {
        if (entry.back) {
            var trail = trayMenuTrail.slice()
            trayMenuParent = trail.pop()
            trayMenuTrail = trail
        } else if (entry.submenu) {
            trayMenuTrail = trayMenuTrail.concat([trayMenuParent])
            trayMenuParent = entry.id
        } else {
            shell.tray.clickMenu(trayMenuKey, entry.id)
            trayMenuKey = ""
        }
    }
    // The entries shown; `revision` only makes the binding read them again when it changes.
    function trayEntries(key, parent, trail, revision) {
        var entries = key === "" ? [] : shell.tray.menu(key, parent)
        return trail.length > 0 ? [{ id: -2, back: true, label: "\u2039 Back", enabled: true, separator: false,
                                     toggle: "", checked: false, icon: "", submenu: false }].concat(entries)
                                : entries
    }
    // The application is told which level of its menu is on screen (AboutToShow and "opened")
    // and when it no longer is ("closed"), once a change has settled.
    property string trayShownKey: ""
    property int trayShownParent: 0
    function syncTrayMenu() {
        if (trayShownKey === trayMenuKey && trayShownParent === trayMenuParent)
            return
        if (trayShownKey !== "")
            shell.tray.closeMenu(trayShownKey, trayShownParent)
        trayShownKey = trayMenuKey
        trayShownParent = trayMenuParent
        if (trayMenuKey !== "")
            shell.tray.openMenu(trayMenuKey, trayMenuParent)
    }
    onTrayMenuKeyChanged: Qt.callLater(syncTrayMenu)
    onTrayMenuParentChanged: Qt.callLater(syncTrayMenu)
    Connections {
        target: shell.tray
        function onActivationRefused(key) {
            var button = root.trayActivating
            root.trayActivating = null
            if (button && button.key === key)
                root.trayMenu(button)
        }
        function onMenuChanged(key) {
            if (key !== root.trayMenuKey)
                return
            if (shell.tray.contains(key))
                root.trayMenuRevision++
            else
                root.trayMenuKey = ""
        }
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

    MouseArea {
        anchors.fill: parent
        visible: root.menuOpen
        onClicked: root.closeMenus()
    }

    // Left-clicking the volume control: the default output's volume, then each application's.
    Loader {
        id: mixerLoader
        asynchronous: !(root.audioPopup === "mixer")
        active: root.audioPopup === "mixer" || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { AudioMixer { panel: root; barItem: bar } }
    }

    // Clicking the clock: a month calendar with the current day marked.
    Loader {
        id: calendarLoader
        asynchronous: !(root.audioPopup === "calendar")
        active: root.audioPopup === "calendar" || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { CalendarPopup { panel: root; barItem: bar } }
    }

    // Right-clicking the volume control: the outputs to play through.
    Loader {
        id: outputsLoader
        asynchronous: !(root.audioPopup === "outputs")
        active: root.audioPopup === "outputs" || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { AudioOutputs { panel: root; barItem: bar } }
    }

    // The profile button: the appearance profiles, the one in use marked.
    Loader {
        id: profilesLoader
        asynchronous: !(root.audioPopup === "profiles")
        active: root.audioPopup === "profiles" || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { ProfileList { panel: root; barItem: bar } }
    }

    // The wallpaper button: thumbnails of the pictures in shell.wallpapers, by subfolder, with
    // a filter; clicking one shows it at once and keeps the picker open to try another.
    Loader {
        id: wallpapersLoader
        asynchronous: !(root.audioPopup === "wallpapers")
        active: root.audioPopup === "wallpapers" || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component {
            Rectangle {
                id: wallpaperPicker
                parent: root
                objectName: "wallpaperPicker"
                visible: root.audioPopup === "wallpapers"
                // "" shows every folder.
                property string folder: ""
                readonly property var folders: {
                    var seen = [], list = shell.wallpapers
                    for (var i = 0; i < list.length; ++i)
                        if (seen.indexOf(list[i].folder) < 0) seen.push(list[i].folder)
                    return seen
                }
                readonly property var shown: {
                    var words = filter.text.toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0 })
                    var folder = wallpaperPicker.folder
                    return shell.wallpapers.filter(function(w) {
                        if (folder !== "" && w.folder !== folder) return false
                        var text = (w.folder + "/" + w.name).toLowerCase()
                        return words.every(function(word) { return text.indexOf(word) >= 0 })
                    })
                }
                function opened() { shell.findWallpapers(); filter.forceActiveFocus() }
                onVisibleChanged: if (visible) opened()
                Component.onCompleted: if (visible) opened()
                width: Math.min(880, root.width - 16)
                height: Math.max(220, Math.min(500, root.height - bar.height - shell.panelMarginTop - shell.panelMarginBottom - 16))
                x: Math.max(8, Math.min(root.audioPopupX - width / 2, root.width - width - 8))
                y: root.onTop ? bar.y + bar.height + 8 : bar.y - height - 8
                color: shell.panelColor; radius: 10
                border.color: Qt.lighter(shell.panelColor, 1.6)
                MouseArea { anchors.fill: parent }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Text {
                            text: "Wallpapers"; color: shell.textColor; font.pixelSize: shell.fontSize + 1; font.bold: true; font.family: root.uiFont
                        }
                        Text {
                            Layout.fillWidth: true
                            text: wallpaperPicker.shown.length + (wallpaperPicker.shown.length === 1 ? " picture" : " pictures")
                            elide: Text.ElideRight
                            color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: root.uiFont
                        }
                        TextField {
                            id: filter
                            objectName: "wallpaperFilter"
                            Layout.preferredWidth: 220; Layout.preferredHeight: 30
                            placeholderText: "Filter"
                            color: shell.textColor; placeholderTextColor: Qt.darker(shell.textColor, 1.6)
                            font.pixelSize: shell.fontSize; font.family: root.uiFont
                            background: Rectangle { radius: 6; color: Qt.lighter(shell.panelColor, 1.35); border.color: filter.activeFocus ? shell.accent : "transparent" }
                            Keys.onEscapePressed: root.closeMenus()
                            Keys.onReturnPressed: if (wallpaperPicker.shown.length > 0) shell.pickWallpaper(wallpaperPicker.shown[0].path)
                        }
                        Button {
                            id: shuffle
                            objectName: "wallpaperShuffle"
                            Layout.preferredWidth: 30; Layout.preferredHeight: 30
                            enabled: wallpaperPicker.shown.length > 0
                            Accessible.name: "Random wallpaper"
                            onClicked: shell.pickWallpaper(wallpaperPicker.shown[Math.floor(Math.random() * wallpaperPicker.shown.length)].path)
                            background: Rectangle { radius: 6; color: shuffle.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent" }
                            contentItem: Item { Icon { anchors.centerIn: parent; name: "shuffle"; size: 16 } }
                        }
                    }
                    // The subfolders, as tabs.
                    ListView {
                        id: folderTabs
                        Layout.fillWidth: true; Layout.preferredHeight: 28
                        visible: wallpaperPicker.folders.length > 1
                        orientation: ListView.Horizontal; spacing: 6; clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: [""].concat(wallpaperPicker.folders)
                        delegate: Button {
                            id: folderTab
                            required property string modelData
                            readonly property bool current: modelData === wallpaperPicker.folder
                            height: 28
                            onClicked: wallpaperPicker.folder = modelData
                            background: Rectangle {
                                radius: 14
                                color: folderTab.current ? shell.accent : (folderTab.hovered ? Qt.lighter(shell.panelColor, 1.55) : Qt.lighter(shell.panelColor, 1.25))
                            }
                            contentItem: Text {
                                leftPadding: 6; rightPadding: 6
                                text: folderTab.modelData === "" ? "All" : folderTab.modelData
                                verticalAlignment: Text.AlignVCenter
                                color: folderTab.current ? shell.panelColor : shell.textColor
                                font.pixelSize: shell.fontSize - 1; font.family: root.uiFont
                            }
                        }
                        WheelHandler {
                            onWheel: (event) => {
                                var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                                folderTabs.contentX = Math.max(0, Math.min(folderTabs.contentWidth - folderTabs.width, folderTabs.contentX - delta))
                            }
                        }
                    }
                    GridView {
                        id: wallpaperGrid
                        objectName: "wallpaperGrid"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        readonly property int columns: Math.max(2, Math.floor(width / 200))
                        cellWidth: Math.floor(width / columns)
                        cellHeight: Math.floor((cellWidth - 8) * 9 / 16) + 8
                        model: wallpaperPicker.shown
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Text {
                            anchors.centerIn: parent
                            visible: wallpaperGrid.count === 0
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                            text: shell.wallpapers.length === 0 ? "No pictures in " + shell.wallpaperFolder : "Nothing matches"
                            color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize; font.family: root.uiFont
                        }
                        delegate: Button {
                            id: thumb
                            objectName: "wallpaperItem"
                            required property var modelData
                            readonly property bool current: modelData.path === shell.wallpaperFile
                            width: wallpaperGrid.cellWidth; height: wallpaperGrid.cellHeight
                            Accessible.name: modelData.name + (current ? ", in use" : "")
                            onClicked: shell.pickWallpaper(modelData.path)
                            background: Item {}
                            contentItem: Item {
                                Rectangle {
                                    anchors.fill: parent; anchors.margins: 4
                                    radius: 6
                                    color: Qt.lighter(shell.panelColor, 1.25)
                                    Image {
                                        anchors.fill: parent; anchors.margins: 2
                                        source: "image://thumbs/" + encodeURIComponent(thumb.modelData.path)
                                        sourceSize: Qt.size(256, 256)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        smooth: true
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 150 } }
                                    }
                                    // The name, over the picture while hovered (bar tooltips stay
                                    // hidden while a popup is open).
                                    Rectangle {
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                        anchors.margins: 2
                                        height: nameText.implicitHeight + 6
                                        visible: thumb.hovered
                                        color: Qt.rgba(shell.panelColor.r, shell.panelColor.g, shell.panelColor.b, 0.85)
                                        Text {
                                            id: nameText
                                            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                                            verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle
                                            text: thumb.modelData.name
                                            color: shell.textColor; font.pixelSize: shell.fontSize - 2; font.family: root.uiFont
                                        }
                                    }
                                    // The border sits over the picture: the software renderer
                                    // cannot clip it to rounded corners.
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6; color: "transparent"
                                        border.width: thumb.current ? 3 : (thumb.hovered ? 2 : 0)
                                        border.color: thumb.current ? shell.accent : Qt.rgba(shell.textColor.r, shell.textColor.g, shell.textColor.b, 0.7)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The bell's notification history.
    Loader {
        id: historyLoader
        asynchronous: !(root.audioPopup === "notifications")
        active: shell.notifications.serving && (root.audioPopup === "notifications" || root.warm || used)
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { NotificationHistory { panel: root; barItem: bar } }
    }

    Loader {
        id: launcherLoader
        asynchronous: !(root.launcherOpen)
        active: root.launcherOpen || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component {
            Rectangle {
                id: launcher
                parent: root
                objectName: "launcher"
                visible: root.launcherOpen
                function opened() { search.text = ""; takeFocus() }
                // The keyboard goes to the power menu while it is open, else to the search field.
                function takeFocus() {
                    if (root.powerOpen) { powerMenu.current = 0; powerMenu.forceActiveFocus() }
                    else search.forceActiveFocus()
                }
                onVisibleChanged: if (visible) opened()
                Component.onCompleted: if (visible) opened()
                Connections {
                    target: root
                    function onPowerOpenChanged() { if (launcher.visible) launcher.takeFocus() }
                }
                width: Math.min(460, root.width - 24)
                height: root.height - shell.panelExtent - 20
                anchors.left: parent.left
                anchors.leftMargin: 12 + shell.panelMarginLeft
                y: root.onTop ? bar.y + bar.height + 10 : bar.y - height - 10
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
                                Text { text: modelData.name; textFormat: Text.PlainText; color: shell.textColor; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true; font.family: root.uiFont }
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
                    RowLayout {
                        id: launcherFooter
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "shaodesk"
                            color: Qt.darker(shell.textColor, 1.7)
                            font.pixelSize: 11; font.family: root.uiFont
                        }
                        // Lock, suspend and the rest, as far as the compositor says they may run.
                        Button {
                            id: powerButton
                            objectName: "powerButton"
                            visible: shell.widgets.power && shell.power.available.length > 0
                            Layout.preferredWidth: 36; Layout.preferredHeight: 36
                            onClicked: root.powerOpen = !root.powerOpen
                            Accessible.name: "Power"
                            ToolTip.text: "Lock, suspend, power off"
                            ToolTip.visible: hovered && !root.powerOpen
                            ToolTip.delay: 500
                            background: Rectangle {
                                radius: 7
                                color: root.powerOpen ? Qt.lighter(shell.panelColor, 1.8) : (powerButton.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                            }
                            contentItem: Item {
                                Icon { anchors.centerIn: parent; name: "power"; color: root.powerOpen ? shell.accent : shell.textColor }
                            }
                        }
                    }
                }
                // While the power menu is open, a press anywhere else in the launcher closes it.
                MouseArea {
                    anchors.fill: parent
                    z: 1
                    visible: root.powerOpen
                    onPressed: root.powerOpen = false
                }
                // The power menu, above the power button. Restart, power off and log out ask
                // first (PowerDialog.qml).
                Rectangle {
                    id: powerMenu
                    objectName: "powerMenu"
                    z: 2
                    // The entry Up and Down move to and Enter runs.
                    property int current: 0
                    function run(index) {
                        var entry = shell.power.entries[index]
                        // Closing the launcher first hands the keyboard back before it runs.
                        root.closeMenus()
                        if (entry)
                            shell.power.request(entry.action, outputName)
                    }
                    visible: root.powerOpen
                    Keys.onUpPressed: current = (current + shell.power.entries.length - 1) % Math.max(1, shell.power.entries.length)
                    Keys.onDownPressed: current = (current + 1) % Math.max(1, shell.power.entries.length)
                    Keys.onReturnPressed: run(current)
                    Keys.onEnterPressed: run(current)
                    width: 220; height: 12 + shell.power.entries.length * 44 + Math.max(0, shell.power.entries.length - 1) * 2
                    anchors.right: parent.right; anchors.rightMargin: 14
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 20 + launcherFooter.height + 6
                    color: Qt.lighter(shell.panelColor, 1.2); radius: 10
                    border.color: Qt.lighter(shell.panelColor, 1.8)
                    MouseArea { anchors.fill: parent }
                    Column {
                        anchors.fill: parent; anchors.margins: 6; spacing: 2
                        Repeater {
                            model: shell.power.entries
                            delegate: Button {
                                id: powerItem
                                required property var modelData
                                required property int index
                                objectName: "powerItem:" + modelData.action
                                width: parent.width; height: 44
                                text: modelData.title
                                focusPolicy: Qt.NoFocus
                                palette.buttonText: shell.textColor
                                onClicked: powerMenu.run(index)
                                onHoveredChanged: if (hovered) powerMenu.current = index
                                background: Rectangle { color: powerItem.hovered || powerMenu.current === powerItem.index ? Qt.lighter(shell.panelColor, 1.6) : "transparent"; radius: 6 }
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: contextMenuLoader
        asynchronous: !(root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen)
        active: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component {
            Rectangle {
                id: contextMenu
                parent: root
                objectName: "contextMenu"
                readonly property var actions: root.taskMenuId >= 0
                    ? [{ text: "Maximize / restore", run: function(id) { shell.tasks.maximize(id) } },
                       { text: "Minimize", run: function(id) { shell.tasks.minimize(id) } }]
                      .concat(root.taskMenuApp ? [root.pinAction(root.taskMenuApp)] : [])
                      .concat([{ text: "Close window", run: function(id) { shell.tasks.close(id) } }])
                    : root.pinMenuApp !== null
                    ? [{ text: "Open " + root.pinMenuApp.name, run: function() { shell.launch(root.pinMenuApp.appId) } }]
                      .concat(root.pinMenuApp.configured ? [] : [root.pinAction(root.pinMenuApp.appId)])
                    : root.profileMenu
                    ? [{ text: "‹ Back", run: function() { root.profileMenu = false; return true } }]
                      .concat(shell.profiles.map(function(name) {
                          return { text: (name === shell.profile ? "✓ " : "") + name,
                                   run: function() { shell.pickProfile(name) } } }))
                    : [{ text: root.tiling ? "Turn tiling off" : "Turn tiling on", enabled: shell.tilingAvailable,
                         run: function() { shell.toggleTiling(outputName) } },
                       { text: "Applications", run: function() { root.launcherOpen = true } },
                       { text: "Show desktop", run: function() { shell.tasks.showDesktop() } }]
                      .concat(shell.profiles.length > 0
                          ? [{ text: "Appearance: " + (shell.profile || "none") + " …",
                               run: function() { root.profileMenu = true; return true } }]
                          : [])
                visible: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen
                width: 220; height: 12 + actions.length * 44 + (actions.length - 1) * 2
                x: Math.max(8, Math.min(root.contextMenuX, root.width - width - 8))
                y: root.onTop ? bar.y + bar.height + 8 : bar.y - height - 8
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
                                // Running an entry can rebuild the entries, destroying this button.
                                var panel = root
                                // An entry that leads to more entries returns true to stay open.
                                if (modelData.run(panel.taskMenuId) === true)
                                    return
                                panel.taskMenuId = -1; panel.pinMenuApp = null; panel.barMenuOpen = false
                            }
                        }
                    }
                }
            }
        }
    }

    // A tray item's menu, in the style of the bar's own: a submenu's entries take the place of
    // the menu's, with a way back, as the bar menu's appearance entry does. A long one scrolls.
    Loader {
        id: trayMenuLoader
        asynchronous: root.trayMenuKey === ""
        active: root.trayMenuKey !== "" || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component {
            Rectangle {
                id: trayMenu
                parent: root
                objectName: "trayMenu"
                readonly property int rowHeight: 34
                readonly property var entries: root.trayEntries(root.trayMenuKey, root.trayMenuParent,
                                                                root.trayMenuTrail, root.trayMenuRevision)
                readonly property real widest: {
                    var widest = 0
                    for (var i = 0; i < entries.length; ++i)
                        widest = Math.max(widest, menuFont.advanceWidth(entries[i].label))
                    return widest
                }
                readonly property real listHeight: {
                    var sum = 0
                    for (var i = 0; i < entries.length; ++i)
                        sum += entries[i].separator ? 9 : rowHeight
                    return Math.max(rowHeight, sum)
                }
                FontMetrics { id: menuFont; font.pixelSize: shell.fontSize; font.family: root.uiFont }
                visible: root.trayMenuKey !== ""
                width: Math.min(root.width - 16, Math.max(180, widest + 80))
                height: Math.min(root.height - shell.panelExtent - 20, 12 + listHeight)
                x: Math.max(8, Math.min(root.trayMenuX - width / 2, root.width - width - 8))
                y: root.onTop ? bar.y + bar.height + 8 : bar.y - height - 8
                color: shell.panelColor; radius: 10
                border.color: Qt.lighter(shell.panelColor, 1.6)
                MouseArea { anchors.fill: parent }
                ListView {
                    id: trayEntryList
                    anchors.fill: parent; anchors.margins: 6
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: trayMenu.entries
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Button {
                        id: trayEntry
                        required property var modelData
                        objectName: modelData.separator ? "trayMenuSeparator" : "trayMenuItem"
                        width: ListView.view.width
                        height: modelData.separator ? 9 : trayMenu.rowHeight
                        text: modelData.label
                        enabled: !modelData.separator && modelData.enabled
                        leftPadding: 4; rightPadding: 6
                        Accessible.name: modelData.label + (modelData.toggle !== "" ? (modelData.checked ? ", checked" : ", not checked") : "")
                        onClicked: root.trayMenuPick(modelData)
                        background: Rectangle {
                            radius: 6
                            color: trayEntry.hovered && trayEntry.enabled ? Qt.lighter(shell.panelColor, 1.5) : "transparent"
                            Rectangle {
                                visible: trayEntry.modelData.separator
                                anchors.verticalCenter: parent.verticalCenter
                                x: 8; width: parent.width - 16; height: 1
                                color: Qt.lighter(shell.panelColor, 1.8)
                            }
                        }
                        contentItem: RowLayout {
                            visible: !trayEntry.modelData.separator
                            spacing: 8
                            opacity: trayEntry.enabled ? 1 : 0.4
                            // A check box, a radio button's dot, or the entry's icon.
                            Item {
                                Layout.preferredWidth: 16; Layout.preferredHeight: 16
                                Rectangle {
                                    visible: trayEntry.modelData.toggle === "radio"
                                    anchors.centerIn: parent
                                    width: 10; height: 10; radius: 5
                                    color: trayEntry.modelData.checked ? shell.accent : "transparent"
                                    border.color: trayEntry.modelData.checked ? shell.accent : Qt.lighter(shell.panelColor, 2.2)
                                }
                                Rectangle {
                                    visible: trayEntry.modelData.toggle === "checkmark"
                                    anchors.centerIn: parent
                                    width: 13; height: 13; radius: 3
                                    color: trayEntry.modelData.checked ? shell.accent : "transparent"
                                    border.color: trayEntry.modelData.checked ? shell.accent : Qt.lighter(shell.panelColor, 2.2)
                                    Text {
                                        visible: trayEntry.modelData.checked
                                        anchors.centerIn: parent
                                        text: "\u2713"; color: shell.panelColor; font.pixelSize: 10; font.bold: true
                                    }
                                }
                                Image {
                                    visible: trayEntry.modelData.toggle === "" && trayEntry.modelData.icon !== ""
                                    anchors.centerIn: parent
                                    width: 16; height: 16
                                    source: visible ? trayEntry.modelData.icon : ""
                                    sourceSize: Qt.size(16, 16)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: trayEntry.modelData.label; textFormat: Text.PlainText; elide: Text.ElideRight
                                color: shell.textColor; font.pixelSize: shell.fontSize; font.family: root.uiFont
                            }
                            Text {
                                visible: trayEntry.modelData.submenu
                                text: "\u203a"; color: shell.textColor; font.pixelSize: shell.fontSize + 4
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: trayMenu.entries.length === 0
                        text: "No entries"
                        color: Qt.darker(shell.textColor, 1.4); font.pixelSize: shell.fontSize - 1; font.family: root.uiFont
                    }
                }
            }
        }
    }

    // The windows of the hovered stacked button: clicking one focuses it (or minimizes it when
    // focused already), the cross or a middle click closes it, and a right click opens its menu.
    Loader {
        id: groupListLoader
        asynchronous: !(root.groupOpen)
        active: root.groupOpen || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component {
            Rectangle {
                id: groupList
                parent: root
                objectName: "groupList"
                readonly property int rowHeight: 40
                visible: root.groupOpen
                width: 280; height: 12 + groupWindows.count * rowHeight + Math.max(0, groupWindows.count - 1) * 2
                x: Math.max(8, Math.min(root.groupX - width / 2, root.width - width - 8))
                y: root.onTop ? bar.y + bar.height + 8 : bar.y - height - 8
                color: shell.panelColor; radius: 10
                border.color: Qt.lighter(shell.panelColor, 1.6)
                HoverHandler {
                    id: groupHover
                    onHoveredChanged: if (hovered) groupHide.stop(); else groupHide.restart()
                }
                readonly property bool hovered: groupHover.hovered
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
                            required property bool urgent
                            objectName: "groupWindow"
                            width: parent.width; height: groupList.rowHeight
                            Accessible.name: title
                            // Closing the list destroys this row, so it goes last.
                            onClicked: { shell.tasks.activate(taskId); root.groupOpen = false }
                            background: Rectangle {
                                radius: 6
                                color: groupWindow.hovered ? Qt.lighter(shell.panelColor, 1.5) : (groupWindow.active ? Qt.lighter(shell.panelColor, 1.3) : "transparent")
                                Rectangle { visible: groupWindow.active; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1; color: shell.accent }
                                Rectangle { objectName: "groupWindowUrgent"; visible: groupWindow.urgent; x: 0; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1; color: shell.urgentColor }
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
                                    text: groupWindow.title; textFormat: Text.PlainText; elide: Text.ElideRight
                                    color: groupWindow.urgent ? shell.urgentColor : groupWindow.minimized ? Qt.darker(shell.textColor, 1.4) : shell.textColor
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
        }
    }

    Rectangle {
        id: bar
        objectName: "bar"
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
        // Scrolling the bar anywhere its widgets leave the wheel alone pages through this
        // output's workspaces: a wheel notch (or a touchpad's worth of travel) moves one,
        // stopping at either end; down or right goes to the next.
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
                    // The transform's own x, not the item's: it does not fight the layout.
                    // qmllint disable Quick.layout-positioning
                    transform: Translate { x: pinnedSlot.dragging ? pinnedSlot.dragX : pinnedSlot.shiftX }
                    // qmllint enable Quick.layout-positioning
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
            // windows. Clicking a number switches to it; scrolling pages through them, as it does
            // anywhere on the bar.
            Row {
                id: workspaceIndicator
                objectName: "workspaceIndicator"
                readonly property var workspaceState: shell.workspaces[outputName] || ({ current: 1, occupied: [] })
                function show(number) {
                    if (number >= 1 && number <= shell.workspaceCount && number !== workspaceState.current)
                        shell.showWorkspace(outputName, number)
                }
                visible: shell.workspaceCount > 1 && shell.widgets.workspaces
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
                        // A window on this workspace is asking for attention.
                        readonly property bool urgent: (workspaceIndicator.workspaceState.urgent || []).indexOf(number) >= 0
                        readonly property string label: shell.workspaceNames[index] || ""
                        objectName: "workspace" + number
                        width: label ? Math.max(26, workspaceText.implicitWidth + 14) : 26
                        height: bar.height - 14
                        onClicked: { root.closeMenus(); workspaceIndicator.show(number) }
                        Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
                BarTip { panel: root; owner: workspaceButton; text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "") }
                        background: Rectangle {
                            radius: 6
                            color: workspaceButton.current ? Qt.lighter(shell.panelColor, 1.8) : (workspaceButton.hovered ? Qt.lighter(shell.panelColor, 1.4) : "transparent")
                        }
                        contentItem: Item {
                            Text {
                                id: workspaceText
                                anchors.centerIn: parent
                                text: workspaceButton.label ? workspaceButton.label : workspaceButton.number
                                color: workspaceButton.urgent && !workspaceButton.current ? shell.urgentColor : workspaceButton.current ? shell.accent : shell.textColor
                                font.pixelSize: shell.fontSize; font.family: root.uiFont
                                font.weight: workspaceButton.current ? Font.DemiBold : Font.Normal
                            }
                            Rectangle {
                                objectName: "workspaceUrgent" + workspaceButton.number
                                visible: workspaceButton.urgent
                                anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 2
                                width: 6; height: 6; radius: 3
                                color: shell.urgentColor
                                SequentialAnimation on opacity {
                                    running: workspaceButton.urgent
                                    loops: 6; alwaysRunToEnd: true
                                    NumberAnimation { to: 0.3; duration: 450 }
                                    NumberAnimation { to: 1; duration: 450 }
                                }
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
            }
            // The wallpaper picker.
            Button {
                id: wallpapersButton
                objectName: "wallpapersButton"
                visible: shell.widgets.wallpapers
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                onClicked: root.toggleAudioPopup("wallpapers", wallpapersButton)
                Accessible.name: "Wallpapers"
                BarTip { panel: root; owner: wallpapersButton; text: "Wallpapers" }
                background: Rectangle {
                    radius: 7
                    color: root.audioPopup === "wallpapers" ? Qt.lighter(shell.panelColor, 1.8) : (wallpapersButton.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "image"; color: root.audioPopup === "wallpapers" ? shell.accent : shell.textColor }
                }
            }
            // The appearance profile in use; clicking lists the profiles to switch to.
            Button {
                id: profilesButton
                objectName: "profilesButton"
                visible: shell.widgets.profiles && shell.profiles.length > 1
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                onClicked: root.toggleAudioPopup("profiles", profilesButton)
                Accessible.name: "Appearance: " + (shell.profile || "none")
                BarTip { panel: root; owner: profilesButton; text: "Appearance: " + (shell.profile || "none") }
                background: Rectangle {
                    radius: 7
                    color: root.audioPopup === "profiles" ? Qt.lighter(shell.panelColor, 1.8) : (profilesButton.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                // Three swatches of the profile in use: accent, desktop background, text.
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent; spacing: 2
                        Repeater {
                            model: [shell.accent, shell.background, shell.textColor]
                            // A ring in the text colour keeps a swatch close to the panel's own
                            // colour visible.
                            Rectangle {
                                required property color modelData
                                width: 10; height: 10; radius: 5
                                color: modelData
                                border.width: 1
                                border.color: Qt.rgba(shell.textColor.r, shell.textColor.g, shell.textColor.b, 0.5)
                            }
                        }
                    }
                }
            }
            Button {
                id: tilingToggle
                objectName: "tilingToggle"
                visible: shell.widgets.tiling
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                enabled: shell.tilingAvailable
                opacity: enabled ? 1 : 0.4
                onClicked: { root.closeMenus(); shell.toggleTiling(outputName) }
                Accessible.name: root.tiling ? "Tiling on" : "Tiling off"
                BarTip { panel: root; owner: tilingToggle; text: root.tiling ? "Tiling on: click for floating" : "Floating: click to tile" }
                background: Rectangle {
                    radius: 7
                    color: root.tiling ? Qt.lighter(shell.panelColor, 1.8) : (tilingToggle.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                // On: a split layout in the accent colour. Off: two overlapping windows.
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: root.tiling ? "layout-panel-left" : "copy"
                        color: root.tiling ? shell.accent : shell.textColor
                    }
                }
            }
            // The system tray: the status icons of applications, in the order they appeared.
            // Passive ones stay hidden, and so does the tray when none is left.
            Row {
                id: tray
                objectName: "tray"
                visible: shell.widgets.tray && shell.tray.shown > 0
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    model: shell.tray
                    delegate: TrayButton { panel: root }
                }
            }
            NotificationBell { id: bell; panel: root; barHeight: bar.height }
            NetworkWidget { panel: root; barHeight: bar.height }
            BatteryWidget { panel: root; barHeight: bar.height }
            // The default output's volume. Left-click: per-application volumes; right-click: the
            // output; wheel: louder or quieter; middle-click: mute.
            Button {
                id: audioWidget
                objectName: "audioWidget"
                visible: root.audioSource.available && shell.widgets.volume
                Layout.preferredWidth: 60; Layout.preferredHeight: bar.height - 10
                onClicked: root.toggleAudioPopup("mixer", audioWidget)
                Accessible.name: "Volume " + root.audioSource.volume + "%" + (root.audioSource.muted ? ", muted" : "")
                BarTip { panel: root; owner: audioWidget; text: audioWidget.Accessible.name }
                background: Rectangle {
                    radius: 7
                    color: (root.audioPopup === "mixer" || root.audioPopup === "outputs") ? Qt.lighter(shell.panelColor, 1.8) : (audioWidget.hovered ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
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
            KeyboardLayout { panel: root; barHeight: bar.height }
            // The time and date. Clicking it opens the month calendar.
            Button {
                id: clockButton
                objectName: "clockButton"
                visible: shell.widgets.clock
                Layout.preferredWidth: clock.implicitWidth + 12; Layout.preferredHeight: bar.height - 10
                hoverEnabled: true
                enabled: shell.widgets.calendar
                onClicked: root.toggleAudioPopup("calendar", clockButton)
                Accessible.name: Qt.formatDateTime(clock.now, "dddd d MMMM yyyy, HH:mm")
                background: Rectangle {
                    radius: 7
                    color: root.audioPopup === "calendar" ? Qt.lighter(shell.panelColor, 1.8) : (clockButton.hovered && clockButton.enabled ? Qt.lighter(shell.panelColor, 1.55) : "transparent")
                }
                BarTip { panel: root; owner: clockButton; text: Qt.formatDate(clock.now, "dddd d MMMM yyyy") }
                contentItem: Text {
                    id: clock
                    objectName: "clock"
                    property date now: new Date()
                    text: Qt.formatTime(now, "HH:mm")
                    color: shell.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    font.pixelSize: shell.fontSize; font.weight: Font.DemiBold; font.family: root.uiFont
                    // The clock shows minutes, so it wakes once a minute, just after the minute changes.
                    Timer {
                        id: tick
                        objectName: "clockTick"
                        function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
                        interval: untilMinute(); running: true; repeat: true
                        onTriggered: { clock.now = new Date(); interval = untilMinute() }
                    }
                }
            }
            Button {
                id: showDesktopButton
                Layout.preferredWidth: 14; Layout.fillHeight: true
                onClicked: { root.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                BarTip { panel: root; owner: showDesktopButton; text: "Show desktop" }
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
