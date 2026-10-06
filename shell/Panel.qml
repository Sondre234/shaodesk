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
    // The pointer entering the list keeps it, and leaving it hides it a moment later.
    function hoverGroupList(hovered) {
        if (hovered) groupHide.stop(); else groupHide.restart()
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
        sourceComponent: Component { WallpaperPicker { panel: root; barItem: bar } }
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
        sourceComponent: Component { Launcher { panel: root; barItem: bar } }
    }

    Loader {
        id: contextMenuLoader
        asynchronous: !(root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen)
        active: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen || root.warm || used
        // Once made, a popup stays, so closing it never destroys the item its handler runs in.
        property bool used: false
        onLoaded: used = true
        sourceComponent: Component { TaskbarMenu { panel: root; barItem: bar } }
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
        sourceComponent: Component { TrayMenu { panel: root; barItem: bar } }
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
        sourceComponent: Component { GroupList { panel: root; barItem: bar } }
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
        color: Theme.bar
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
            PinnedSlots { id: pinnedSlots; panel: root; barHeight: bar.height }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Qt.lighter(shell.panelColor, 1.8) }
            TaskList { id: taskList; panel: root; barHeight: bar.height }
            WorkspaceIndicator { id: workspaceIndicator; panel: root; barHeight: bar.height }
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
            VolumeButton { id: audioWidget; panel: root; barHeight: bar.height }
            KeyboardLayout { panel: root; barHeight: bar.height }
            ClockButton { id: clockButton; panel: root; barHeight: bar.height }
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
