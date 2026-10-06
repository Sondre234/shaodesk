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
    onWarmChanged: if (warm) popover.prepare()
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
    // Battery and network state; tests swap in one that reads a fake sysfs.
    property var statusSource: shell.status
    // The screen backlight, swapped the same way.
    property var backlightSource: shell.backlight
    property string audioPopup: ""
    property real audioPopupX: 0
    // A tray item's menu: the item it belongs to ("" while closed), and where its icon is.
    property string trayMenuKey: ""
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
    // Something is open in the popover. The list shown on hover does not take the keyboard: it
    // opens under a window being typed in.
    readonly property bool expanded: menuOpen || groupOpen
    onMenuOpenChanged: if (menuOpen) groupOpen = false
    // The surface the popups are drawn in, and the bar's edges in its coordinates: the bar's
    // surface lies along its top or bottom edge, across its width.
    readonly property Item popupLayer: popupLayer
    // What Quick Settings opens the wallpaper picker by.
    readonly property Item quickSettingsButton: quickButton
    readonly property real barTop: (onTop ? 0 : popover.height - height) + bar.y
    readonly property real barBottom: barTop + bar.height
    // A popup of the bar opens away from the screen edge the bar is on (PopupCard's side), beside
    // the rectangle barAnchor gives: from `x`, `width` wide, and across the bar.
    readonly property int popupSide: onTop ? Qt.BottomEdge : Qt.TopEdge
    function barAnchor(x, width) { return Qt.rect(x, barTop, width, bar.height) }
    // The output but the bar's strip, where popups stay (PopupCard's bounds).
    readonly property rect popupArea: Qt.rect(0, onTop ? height : 0, popover.width, popover.height - height)
    // Where a list shown on hover takes the pointer: over it and down to the bar, so that the
    // pointer crossing from its button never lands on a window between (which, with focus
    // following the pointer, would take the keyboard).
    function hoverArea(item) {
        var top = onTop ? popupArea.y : item.y
        var bottom = onTop ? item.y + item.height : popupArea.y + popupArea.height
        return Qt.rect(item.x, top, item.width, bottom - top)
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
    // closes, so the popover stays up in between.
    function openContextMenu(item, x, taskId, app) {
        contextMenuX = item.mapToItem(root, x, 0).x
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
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
            openContextMenu(bar, bar.width / 2, -1)
            return true
        case "bar-submenu":
            // The appearance profiles beside the bar menu.
            openContextMenu(bar, bar.width / 2, -1)
            previewSubmenu.menu = contextMenuLoader
            previewSubmenu.start()
            return shell.profiles.length > 0
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
        case "tray-submenu":
            for (i = 0; i < tray.children.length; ++i)
                if (tray.children[i].hasMenu) {
                    trayMenu(tray.children[i])
                    if (name === "tray-submenu") {
                        previewSubmenu.menu = trayMenuLoader
                        previewSubmenu.start()
                    }
                    return true
                }
            return false
        case "calendar":
        case "clock-empty":
        case "calendar-years":
            // The flyout; with no notifications kept, or with the calendar zoomed out to the years.
            if (name === "clock-empty")
                shell.notifications.clearHistory()
            toggleAudioPopup("clock", clockButton)
            if (name === "calendar-years")
                Qt.callLater(function() { clockFlyoutLoader.item.calendar.view = "years" })
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
        case "quick-settings":
            toggleAudioPopup("quick", quickButton)
            return quickButton.visible
        case "notifications":
            // The flyout with the mail application's notifications expanded.
            toggleAudioPopup("clock", clockButton)
            Qt.callLater(function() { clockFlyoutLoader.item.notifications.expanded = { "Mail": true } })
            return shell.notifications.serving
        }
        return false
    }
    // For a preview: opens the first submenu of a menu just opened, once its rows are laid out.
    Timer {
        id: previewSubmenu
        property Loader menu
        interval: 50
        onTriggered: {
            var entries = menu.item ? menu.item.entries : []
            for (var i = 0; i < entries.length; ++i)
                if (entries[i].submenu) {
                    menu.item.openSubmenu(i)
                    return
                }
        }
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
        trayMenuKey = button.key
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false
    }
    // The entries of item `key`'s menu entry `parent` (0 for the top), as a PopupMenu lists them:
    // an entry with children opens them beside it, read as it opens; any other is the
    // application's to carry out. `revision` only makes a binding read them again when it changes.
    function trayEntries(key, parent, revision) {
        if (key === "")
            return []
        var entries = shell.tray.menu(key, parent).map(function(entry) {
            if (entry.separator)
                return { separator: true }
            return { id: entry.id, text: entry.label, icon: entry.icon, enabled: entry.enabled,
                     toggle: entry.toggle, checked: entry.checked,
                     submenu: entry.submenu ? function() { return root.trayEntries(key, entry.id, root.trayMenuRevision) }
                                            : undefined,
                     run: entry.submenu ? undefined : function() { shell.tray.clickMenu(key, entry.id) } }
        })
        return entries.length > 0 ? entries : [{ text: "No entries", enabled: false }]
    }
    // The application is told which levels of its menu are on screen (AboutToShow and "opened"
    // for each that appears) and which no longer are ("closed", the deepest first), once a change
    // has settled: what it was told, as {key, id}.
    property var trayShown: []
    function syncTrayMenu() {
        var menu = trayMenuLoader.item
        var wanted = trayMenuKey === "" ? [] : [0].concat(menu ? menu.openEntries.map(function(entry) { return entry.id }) : [])
        wanted = wanted.map(function(id) { return { key: trayMenuKey, id: id } })
        function among(list, level) {
            return list.some(function(other) { return other.key === level.key && other.id === level.id })
        }
        for (var i = trayShown.length - 1; i >= 0; --i)
            if (!among(wanted, trayShown[i]))
                shell.tray.closeMenu(trayShown[i].key, trayShown[i].id)
        for (i = 0; i < wanted.length; ++i)
            if (!among(trayShown, wanted[i]))
                shell.tray.openMenu(wanted[i].key, wanted[i].id)
        trayShown = wanted
    }
    onTrayMenuKeyChanged: Qt.callLater(syncTrayMenu)
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
    // The overview, the window switcher and the command palette come up over the output's
    // windows, and the popups' surface is above them: what is open here closes, as the start
    // menu does on Windows.
    Connections {
        target: shell
        function onOverviewChanged() { if (shell.overviewOutput === root.outputName) root.closeMenus() }
        function onSwitcherChanged() { if (shell.switcherOutput === root.outputName) root.closeMenus() }
    }
    Connections {
        target: shell.palette
        function onOpenChanged() { if (shell.palette.output === root.outputName) root.closeMenus() }
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
    readonly property bool floating: shell.panelRadius > 0 || shell.panelMarginLeft > 0 ||
                                     shell.panelMarginRight > 0 || shell.panelMarginTop > 0 ||
                                     shell.panelMarginBottom > 0

    // A click on the bar's empty space closes what is open.
    MouseArea {
        anchors.fill: parent
        visible: root.menuOpen
        onClicked: root.closeMenus()
    }

    // The popups' surface, over the whole output (PopoverWindow in view.hpp). While a menu is
    // open it takes the keyboard and every press but those on the bar, and a press beside the
    // popups closes them; the list shown on hover takes only the pointer over it and up to the
    // bar. It stays up while what closed fades out.
    PopoverWindow {
        id: popover
        panel: root.shellView
        keyboard: root.menuOpen
        inputRects: root.menuOpen
            ? [root.popupArea]
            : root.groupOpen && groupListLoader.item ? [root.hoverArea(groupListLoader.item)] : []
        onDismissed: root.closeMenus()
        Timer { id: closing; interval: Theme.durationNormal; onTriggered: if (!root.expanded) popover.open = false }
        Connections {
            target: root
            function onExpandedChanged() {
                if (root.expanded) popover.open = true
                else closing.restart()
            }
        }

        Item {
            id: popupLayer
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: root.closeMenus()
            MouseArea {
                anchors.fill: parent
                enabled: root.menuOpen
                onPressed: root.closeMenus()
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

            // Clicking the clock: the notifications and a month calendar, at the bar's right end.
            Loader {
                id: clockFlyoutLoader
                asynchronous: !(root.audioPopup === "clock")
                active: root.audioPopup === "clock" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { ClockFlyout { panel: root; barItem: bar } }
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

            // A tray item's menu, in the style of the bar's own, its submenus beside it.
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

            // The Quick Settings button: tiles, the volume and brightness, the battery.
            Loader {
                id: quickSettingsLoader
                asynchronous: !(root.audioPopup === "quick")
                active: root.audioPopup === "quick" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { QuickSettings { panel: root; barItem: bar } }
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
        color: Theme.bar
        radius: shell.panelRadius
        // A floating bar gets an outline; a docked one a line along its inner edge.
        border.width: root.floating ? 1 : 0
        border.color: Theme.border
        Rectangle {
            visible: !root.floating
            y: root.onTop ? parent.height - 1 : 0
            width: parent.width; height: 1; color: Theme.border
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
            FlatButton {
                id: start
                Layout.preferredWidth: 44; Layout.preferredHeight: bar.height - 10
                active: root.launcherOpen
                onClicked: root.launcherOpen = !root.launcherOpen
                Accessible.name: "Applications"
                contentItem: Item {
                    Grid {
                        anchors.centerIn: parent; columns: 2; spacing: 3
                        Repeater { model: 4; Rectangle { width: 9; height: 9; radius: 2; color: Theme.accent } }
                    }
                }
            }
            PinnedSlots { id: pinnedSlots; panel: root; barHeight: bar.height }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.divider }
            TaskList { id: taskList; panel: root; barHeight: bar.height }
            WorkspaceIndicator { id: workspaceIndicator; panel: root; barHeight: bar.height }
            // The wallpaper picker.
            FlatButton {
                id: wallpapersButton
                objectName: "wallpapersButton"
                visible: shell.widgets.wallpapers === "bar"
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                active: root.audioPopup === "wallpapers"
                onClicked: root.toggleAudioPopup("wallpapers", wallpapersButton)
                Accessible.name: "Wallpapers"
                BarTip { panel: root; owner: wallpapersButton; text: "Wallpapers" }
                contentItem: Item {
                    Icon { anchors.centerIn: parent; name: "image"; color: wallpapersButton.active ? Theme.accent : Theme.text }
                }
            }
            // The appearance profile in use; clicking lists the profiles to switch to.
            FlatButton {
                id: profilesButton
                objectName: "profilesButton"
                visible: shell.widgets.profiles === "bar" && shell.profiles.length > 1
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                active: root.audioPopup === "profiles"
                onClicked: root.toggleAudioPopup("profiles", profilesButton)
                Accessible.name: "Appearance: " + (shell.profile || "none")
                BarTip { panel: root; owner: profilesButton; text: "Appearance: " + (shell.profile || "none") }
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
                                border.color: Theme.alpha(Theme.text, 0.5)
                            }
                        }
                    }
                }
            }
            FlatButton {
                id: tilingToggle
                objectName: "tilingToggle"
                visible: shell.widgets.tiling === "bar"
                Layout.preferredWidth: 40; Layout.preferredHeight: bar.height - 10
                enabled: shell.tilingAvailable
                opacity: enabled ? 1 : 0.4
                active: root.tiling
                onClicked: { root.closeMenus(); shell.toggleTiling(outputName) }
                Accessible.name: root.tiling ? "Tiling on" : "Tiling off"
                BarTip { panel: root; owner: tilingToggle; text: root.tiling ? "Tiling on: click for floating" : "Floating: click to tile" }
                // On: a split layout in the accent colour. Off: two overlapping windows.
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: root.tiling ? "layout-panel-left" : "copy"
                        color: root.tiling ? Theme.accent : Theme.text
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
            QuickSettingsButton { id: quickButton; panel: root; barHeight: bar.height }
            ClockButton { id: clockButton; panel: root; barHeight: bar.height }
            Button {
                id: showDesktopButton
                Layout.preferredWidth: 14; Layout.fillHeight: true
                onClicked: { root.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                BarTip { panel: root; owner: showDesktopButton; text: "Show desktop" }
                background: Rectangle { color: parent.hovered ? Theme.accent : Theme.border; width: 3; anchors.right: parent.right }
            }
        }
        Rectangle {
            visible: shell.error.length > 0
            anchors.fill: parent; anchors.margins: 4
            color: Theme.dangerSurface; radius: Theme.radiusSmall
            Text { anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 10; text: shell.error; color: Theme.text; elide: Text.ElideRight; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
            FlatButton {
                id: dismiss
                anchors.right: parent.right; height: parent.height; width: 40
                Accessible.name: "Dismiss"
                onClicked: shell.clearError()
                contentItem: Text { text: "×"; color: Theme.text; font.pixelSize: Theme.fontSizeLarge; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
    }
}
