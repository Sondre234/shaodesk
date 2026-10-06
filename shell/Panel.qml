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
    // A stacked button's menu is about all its windows: where its hover list finds them, as
    // {slot, windowApp}; null for one window's.
    property var taskMenuGroup: null
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
    // The middle of the item it opens by, and the item's width.
    property real audioPopupX: 0
    property real audioPopupWidth: 0
    // A tray item's menu: the item it belongs to ("" while closed), and where its icon is.
    property string trayMenuKey: ""
    property real trayMenuX: 0
    property real trayMenuWidth: 0
    // Bumped when the open menu's item changes its entries, so they are read again.
    property int trayMenuRevision: 0
    // The menu bar's own menus in the macOS style: "system", "app" or "window" while one is open,
    // below the item whose left edge is at menuBarMenuX.
    property string menuBarMenu: ""
    property real menuBarMenuX: 0
    property bool menuOpen: launcherOpen || taskMenuId >= 0 || pinMenuApp !== null || barMenuOpen || audioPopup !== "" ||
                            trayMenuKey !== "" || menuBarMenu !== ""
    // Hovering an application's stacked button lists its windows above it: those of the pinned
    // application groupSlot, or with the app id groupWindowApp outside the pinned slots, or the
    // window groupTask alone when it is 0 or more (a button standing for one window).
    property bool groupOpen: false
    property string groupSlot: ""
    property string groupWindowApp: ""
    property int groupTask: -1
    property string groupIcon: ""
    property real groupX: 0
    property Item groupPending: null
    readonly property TaskFilter groupWindows: TaskFilter {
        controller: shell; sourceModel: root.taskSource
        app: root.groupSlot; windowApp: root.groupWindowApp; taskId: root.groupTask
    }
    // In the taskbar style with shell.thumbnails, resting on any button with windows shows them
    // as pictures instead (WindowThumbnails.qml): a tile for each side by side, `thumbnailGap`
    // apart and inside the card, each holding its window's picture `thumbnailPadding` inside it,
    // thumbnailWidth wide: shell.thumbnailSize, or narrower so that the card fits across the
    // output (with PopupCard's margin), but no narrower than 60 % of it. More windows than fit so
    // are listed after all.
    readonly property bool thumbnails: shell.thumbnails && !macos
    readonly property int thumbnailGap: Theme.spacingS
    readonly property int thumbnailPadding: Theme.spacingM
    readonly property real thumbnailWidth: {
        var count = Math.max(1, groupWindows.count)
        var room = popupArea.width - 2 * Theme.spacingM - (count + 1) * thumbnailGap
        return Math.min(shell.thumbnailSize, Math.floor(room / count) - 2 * thumbnailPadding)
    }
    readonly property bool thumbnailsOpen: groupOpen && thumbnails &&
                                           (groupWindows.count < 2 || thumbnailWidth >= 0.6 * shell.thumbnailSize)
    readonly property bool groupListOpen: groupOpen && !thumbnailsOpen
    // What shows the windows: the card of pictures or the list, and whether the pointer is on it.
    readonly property Item groupPopup: thumbnailsOpen ? thumbnailsLoader.item : groupListLoader.item
    readonly property bool groupListHovered: groupPopup ? groupPopup.hovered : false
    // Something is open in the popover. The list shown on hover does not take the keyboard: it
    // opens under a window being typed in.
    readonly property bool expanded: menuOpen || groupOpen
    onMenuOpenChanged: if (menuOpen) { groupShow.stop(); groupOpen = false }
    // The bars the style has: the taskbar (Taskbar.qml), or in the macOS style the dock
    // (Dock.qml) in the panel's surface and the menu bar (TopMenuBar.qml) in a surface of its own
    // along the output's top edge.
    readonly property bool macos: Theme.macos
    readonly property Item taskbar: taskbarLoader.item
    readonly property Item dock: dockLoader.item
    readonly property Item menuBar: menuBarLoader.item
    readonly property MenuBarWindow menuBarWindow: menuBarWindow
    // The bar along the panel's edge, which its popups open by; the panel itself while it is
    // being made.
    readonly property Item bar: taskbar ? taskbar.barItem : dock ? dock.barItem : root
    // The bar with the status widgets (the clock, Quick Settings, the tray, ...), whose popups
    // open by it, and the part that holds them and names the buttons they open by.
    readonly property Item statusBar: macos ? menuBar || root : bar
    readonly property Item statusArea: macos ? menuBar : taskbar
    // The surface the popups are drawn in, and the bar's top edge in its coordinates: the bar's
    // surface lies along its top or bottom edge, across its width, where it begins at surfaceTop.
    readonly property Item popupLayer: popupLayer
    // What Quick Settings opens the wallpaper picker by.
    readonly property Item quickSettingsButton: statusArea ? statusArea.quickSettings : null
    readonly property real surfaceTop: onTop ? 0 : popover.height - height
    readonly property real barTop: surfaceTop + bar.y
    // The menu bar's strip along the output's top edge, none without one.
    readonly property real menuBarHeight: macos ? Theme.menuBarHeight : 0
    // A popup of the bar opens away from the screen edge the bar is on (PopupCard's side), beside
    // the rectangle barAnchor gives: from `x`, `width` wide, and across the bar. In the macOS
    // style the bar with the widgets is the menu bar, and the dock's popups (an application's
    // menu, the windows of one) open above the dock by dockAnchor, which on the taskbar is the
    // bar's.
    readonly property int popupSide: onTop || macos ? Qt.BottomEdge : Qt.TopEdge
    function barAnchor(x, width) { return macos ? Qt.rect(x, 0, width, menuBarHeight) : dockAnchor(x, width) }
    readonly property int dockSide: macos ? Qt.TopEdge : popupSide
    function dockAnchor(x, width) { return Qt.rect(x, barTop, width, bar.height) }
    // The output but the bar's strip, where popups stay (PopupCard's bounds); between the menu
    // bar and the dock's strip in the macOS style.
    readonly property rect popupArea: macos
        ? Qt.rect(0, menuBarHeight, popover.width, popover.height - menuBarHeight - shell.panelExtent)
        : Qt.rect(0, onTop ? height : 0, popover.width, popover.height - height)
    // Where a list shown on hover takes the pointer: over it and down to the bar, so that the
    // pointer crossing from its button never lands on a window between (which, with focus
    // following the pointer, would take the keyboard).
    function hoverArea(item) {
        var top = onTop ? popupArea.y : item.y
        var bottom = onTop ? item.y + item.height : macos ? barTop : popupArea.y + popupArea.height
        return Qt.rect(item.x, top, item.width, bottom - top)
    }
    // Where the popover takes the pointer while a popup is open: all but the bars, so that a
    // press on either switches popups in one press. In the macOS style that is the output but the
    // menu bar's strip and the dock's rectangle, as the four rectangles around the dock; but
    // Launchpad covers both bars, and takes every press.
    readonly property var popoverInput: {
        if (!macos)
            return [popupArea]
        var top = menuBarHeight, w = popover.width, h = popover.height
        if (launcherOpen)
            return [Qt.rect(0, 0, w, h)]
        var left = bar.x, right = bar.x + bar.width, dockBottom = barTop + bar.height
        return [Qt.rect(0, top, w, barTop - top), Qt.rect(0, dockBottom, w, h - dockBottom),
                Qt.rect(0, barTop, left, bar.height), Qt.rect(right, barTop, w - right, bar.height)]
    }
    // An application's record in shell.apps for a window's app id: the application whose pinned
    // slot it takes (a configured launcher too), else the installed one; null when there is none.
    function appRecord(windowAppId) {
        var id = shell.pinnedAppFor(windowAppId) || shell.appFor(windowAppId)
        var apps = id !== "" ? shell.apps : []
        for (var i = 0; i < apps.length; ++i)
            if (apps[i].appId === id)
                return apps[i]
        return null
    }
    onLauncherOpenChanged: {
        if (launcherOpen) { taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = "" }
        else powerOpen = false
    }
    function closeMenus() { launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; trayMenuKey = ""; menuBarMenu = "" }
    // A change of style lays the panel out anew: what was open belonged to bars that are gone.
    onMacosChanged: closeMenus()
    // A stacked button hovered for a moment, or at once while another's list is open, shows its
    // windows; leaving both it and the list hides them again. With thumbnails any window's button
    // does, after shell.thumbnailDelay. Pressing the button hides them until the pointer comes
    // back.
    function showsWindows(button) { return button.stacked || thumbnails }
    function hoverGroup(button, hovered) {
        if (hovered && showsWindows(button) && !menuOpen && !button.pressed) {
            groupPending = button
            groupHide.stop()
            if (groupOpen) showGroup(); else groupShow.restart()
        } else if (!hovered && button === groupPending) {
            // Entering the next button can come before leaving this one.
            groupShow.stop()
            if (groupOpen) groupHide.restart()
        }
    }
    function closeGroup() {
        groupShow.stop()
        groupOpen = false
    }
    function showGroup() {
        var button = groupPending
        if (button && button.hovered && !button.pressed && showsWindows(button) && !menuOpen)
            openGroup(button)
    }
    function openGroup(button) {
        // A taskbar button without a group stands for its own window; a dock icon has none.
        groupTask = button.group === null ? button.taskId : -1
        groupSlot = button.groupSlot
        groupWindowApp = button.groupWindowApp
        groupIcon = button.iconName
        groupX = button.mapToItem(root, button.width / 2, 0).x
        groupOpen = true
    }
    Timer { id: groupShow; interval: root.thumbnails ? shell.thumbnailDelay : 350; onTriggered: root.showGroup() }
    Timer {
        id: groupHide; interval: 300
        onTriggered: if (!root.groupListHovered && !(root.groupPending && root.groupPending.hovered)) root.groupOpen = false
    }
    // The pointer entering the list (or the card) keeps it, and leaving it hides it a moment later.
    function hoverGroupList(hovered) {
        if (hovered) groupHide.stop(); else groupHide.restart()
    }
    // Opens on press, as a desktop context menu does: waiting for a tap lost a press held
    // past the long-press time or moved while held. The new menu opens before the old one
    // closes, so the popover stays up in between.
    function openContextMenu(item, x, taskId, app) {
        contextMenuX = item.mapToItem(root, x, 0).x
        taskMenuGroup = taskId >= 0 && item.stacked === true ? { slot: item.groupSlot, windowApp: item.groupWindowApp } : null
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
        launcherOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = ""
    }
    // Opens (or, when it is already open, closes) one of the volume control's popups.
    function toggleAudioPopup(kind, item) {
        if (audioPopup === kind) { audioPopup = ""; return }
        audioPopupX = item.mapToItem(root, item.width / 2, 0).x
        audioPopupWidth = item.width
        audioPopup = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; trayMenuKey = ""; menuBarMenu = ""
    }
    // Opens (or, when it is already open, closes) one of the menu bar's own menus below `item`.
    function toggleMenuBarMenu(kind, item) {
        if (menuBarMenu === kind) { menuBarMenu = ""; return }
        menuBarMenuX = item.mapToItem(root, 0, 0).x
        menuBarMenu = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = ""
    }
    // While one of them is open, the pointer moving onto another's title opens that one instead.
    function hoverMenuBarMenu(kind, item) {
        if (menuBarMenu !== "" && menuBarMenu !== kind)
            toggleMenuBarMenu(kind, item)
    }
    // The power menu, from the power_menu action: the launcher opens with it, and closes with it
    // when asked again.
    function togglePowerMenu() {
        // The macOS style's system menu has them.
        if (macos) {
            if (menuBarMenu === "system")
                closeMenus()
            else if (menuBar && shell.power.entries.length > 0)
                toggleMenuBarMenu("system", menuBar.systemButton)
            return
        }
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
        if (macos) {
            var shown = previewMacos(name)
            if (shown !== undefined)
                return shown
        }
        var taskList = taskbar ? taskbar.tasks : null, pinnedSlots = taskbar ? taskbar.pins : null
        var tray = statusArea.trayRow, clockButton = statusArea.clock, audioWidget = statusArea.volume
        var quickButton = statusArea.quickSettings
        var profilesButton = statusArea.profiles, wallpapersButton = statusArea.wallpapers
        if (taskList)
            taskList.forceLayout()
        var i
        switch (name) {
        case "bar":
            return true
        case "launcher":
            launcherOpen = true
            return true
        case "launcher-all":
        case "launcher-search":
        case "launcher-empty":
        case "launcher-menu":
            // The start menu's other views, as Launcher.preview names them.
            launcherOpen = true
            return launcherLoader.item.preview(name)
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
            // An installed application's slot when there is one, whose menu has more to show.
            var slot = pinnedSlots.itemAt(0)
            for (i = pinnedSlots.count - 1; i >= 0; --i)
                if (!pinnedSlots.itemAt(i).modelData.configured)
                    slot = pinnedSlots.itemAt(i)
            if (slot)
                openContextMenu(slot, 0, -1, slot.modelData)
            return slot !== null
        case "stack-menu":
            // A stacked button's menu, with the workspaces to move its windows to beside it.
            for (i = 0; i < taskList.count; ++i) {
                var stack = taskList.itemAtIndex(i)
                if (stack && stack.stacked) {
                    openContextMenu(stack, 0, stack.taskId, stack.appId)
                    previewSubmenu.menu = contextMenuLoader
                    previewSubmenu.start()
                    return true
                }
            }
            return false
        case "group":
        case "thumbnails":
            // A stacked button, in the task list or in a pinned slot: its list, or with
            // shell.thumbnails the pictures of its windows (the gallery's group turns them off).
            var buttons = []
            for (i = 0; i < taskList.count; ++i)
                buttons.push(taskList.itemAtIndex(i))
            for (i = 0; i < pinnedSlots.count; ++i)
                buttons = buttons.concat(pinnedSlots.itemAt(i).children)
            for (i = 0; i < buttons.length; ++i)
                if (buttons[i] && buttons[i].stacked) {
                    openGroup(buttons[i])
                    return name === "group" || thumbnailsOpen
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
        case "quick-settings-mixer":
            // Quick Settings; with the applications' volumes open.
            toggleAudioPopup("quick", quickButton)
            if (name === "quick-settings-mixer")
                Qt.callLater(function() { quickSettingsLoader.item.expanded = "mixer" })
            return quickButton.visible
        case "notifications":
            // The flyout with the mail application's notifications expanded.
            toggleAudioPopup("clock", clockButton)
            Qt.callLater(function() { clockFlyoutLoader.item.notifications.expanded = { "Mail": true } })
            return shell.notifications.serving
        }
        return false
    }
    // The popups previewPopup opens otherwise in the macOS style: those of the dock's icons and
    // the menu bar's menus (system-menu, app-menu, window-menu, window-submenu); undefined for
    // the others, which open as on the taskbar.
    function previewMacos(name) {
        var icons = [], i
        for (i = 0; i < dock.pins.count; ++i)
            icons.push(dock.pins.itemAt(i))
        for (i = 0; i < dock.running.count; ++i)
            icons.push(dock.running.itemAt(i))
        // The first icon with windows, with several, or without any.
        function find(test) { return icons.filter(function(icon) { return icon && test(icon) })[0] || null }
        var icon
        switch (name) {
        case "power":
        case "system-menu":
            toggleMenuBarMenu("system", menuBar.systemButton)
            return true
        case "app-menu":
            toggleMenuBarMenu("app", menuBar.appButton)
            return true
        case "window-menu":
        case "window-submenu":
            toggleMenuBarMenu("window", menuBar.windowButton)
            if (name === "window-submenu") {
                previewSubmenu.menu = menuBarMenuLoader
                previewSubmenu.start()
            }
            return true
        case "task-menu":
            icon = find(function(icon) { return icon.running })
            if (icon)
                dock.openMenu(icon, icon.modelData || null, "")
            return icon !== null
        case "pin-menu":
            icon = find(function(icon) { return !icon.running && icon.modelData && !icon.modelData.configured })
            if (icon)
                dock.openMenu(icon, icon.modelData, icon.modelData.appId)
            return icon !== null
        case "stack-menu":
        case "group":
        case "thumbnails":
            // The dock lists a stack's windows, pictures or not.
            icon = find(function(icon) { return icon.stacked })
            if (icon && name !== "stack-menu")
                openGroup(icon)
            else if (icon)
                dock.openMenu(icon, icon.modelData || null, "")
            return icon !== null
        }
        return undefined
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
    // panel spans its output's width, at its top or bottom edge, and the menu bar along its top.
    function trayPoint(item) {
        var p = item.mapToItem(root, item.width / 2, item.height / 2)
        var inMenuBar = item.Window.window === menuBarWindow
        return Qt.point(Math.round(Screen.virtualX + p.x),
                        Math.round(Screen.virtualY + (inMenuBar || onTop ? p.y : Screen.height - height + p.y)))
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
        trayMenuWidth = button.width
        trayMenuKey = button.key
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; menuBarMenu = ""
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
        var toggle = function() { Qt.callLater(function() { if (pinned) shell.unpin(appId); else shell.pin(appId) }) }
        if (macos)
            return { text: pinned ? "Remove from Dock" : "Keep in Dock", objectName: "contextMenuKeep", run: toggle }
        return pinned ? { text: "Unpin from taskbar", icon: "pin-off", run: toggle }
                      : { text: "Pin to taskbar", icon: "pin", run: toggle }
    }
    // Popups open away from the screen edge the bar sits on.
    // Tiling is per monitor: this panel shows and toggles its own.
    readonly property bool tiling: {
        var state = shell.workspaces[outputName]
        return state && state.tiling !== undefined ? state.tiling : shell.tiling
    }
    // The dock is at the bottom whatever shell.panel_position says.
    readonly property bool onTop: shell.panelTop && !macos

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
            ? root.popoverInput
            : root.groupOpen && root.groupPopup ? [root.hoverArea(root.groupPopup)] : []
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
                sourceComponent: Component { AudioMixer { panel: root; barItem: root.statusBar } }
            }

            // Clicking the clock: the notifications and a month calendar, at the bar's right end.
            Loader {
                id: clockFlyoutLoader
                asynchronous: !(root.audioPopup === "clock")
                active: root.audioPopup === "clock" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { ClockFlyout { panel: root; barItem: root.statusBar } }
            }

            // Right-clicking the volume control: the outputs to play through.
            Loader {
                id: outputsLoader
                asynchronous: !(root.audioPopup === "outputs")
                active: root.audioPopup === "outputs" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { AudioOutputs { panel: root; barItem: root.statusBar } }
            }

            // The profile button: the appearance profiles, the one in use marked.
            Loader {
                id: profilesLoader
                asynchronous: !(root.audioPopup === "profiles")
                active: root.audioPopup === "profiles" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { ProfileList { panel: root; barItem: root.statusBar } }
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
                sourceComponent: Component { WallpaperPicker { panel: root; barItem: root.statusBar } }
            }

            Loader {
                id: launcherLoader
                asynchronous: !(root.launcherOpen)
                active: root.launcherOpen || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                // The start menu, or Launchpad in the macOS style.
                sourceComponent: Theme.macos ? launchpadComponent : startMenuComponent
                Component { id: startMenuComponent; Launcher { panel: root; barItem: root.bar } }
                Component { id: launchpadComponent; Launchpad { panel: root; barItem: root.bar } }
            }

            Loader {
                id: contextMenuLoader
                asynchronous: !(root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen)
                active: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { TaskbarMenu { panel: root; barItem: root.bar } }
            }

            // A tray item's menu, in the style of the bar's own, its submenus beside it.
            Loader {
                id: trayMenuLoader
                asynchronous: root.trayMenuKey === ""
                active: root.trayMenuKey !== "" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { TrayMenu { panel: root; barItem: root.statusBar } }
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
                sourceComponent: Component { GroupList { panel: root; barItem: root.bar } }
            }

            // The pictures of the windows of the button the pointer rests on, with shell.thumbnails
            // in the taskbar style.
            Loader {
                id: thumbnailsLoader
                asynchronous: !(root.thumbnailsOpen)
                active: root.thumbnails && (root.groupOpen || root.warm) || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { WindowThumbnails { panel: root; barItem: root.bar } }
            }

            // The Quick Settings button: tiles, the volume and brightness, the battery.
            Loader {
                id: quickSettingsLoader
                asynchronous: !(root.audioPopup === "quick")
                active: root.audioPopup === "quick" || root.warm || used
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { QuickSettings { panel: root; barItem: root.statusBar } }
            }

            // The menu bar's own menus in the macOS style: the system menu, the focused
            // application's and the Window menu.
            Loader {
                id: menuBarMenuLoader
                asynchronous: root.menuBarMenu === ""
                active: root.macos && (root.menuBarMenu !== "" || root.warm || used)
                // Once made, a popup stays, so closing it never destroys the item its handler runs in.
                property bool used: false
                onLoaded: used = true
                sourceComponent: Component { MenuBarMenu { panel: root; barItem: root.statusBar } }
            }
        }
    }

    // The taskbar along the panel's edge, or the dock, as the style has it.
    Loader {
        id: taskbarLoader
        anchors.fill: parent
        active: !root.macos
        sourceComponent: Component { Taskbar { panel: root } }
    }
    Loader {
        id: dockLoader
        anchors.fill: parent
        active: root.macos
        sourceComponent: Component { Dock { panel: root } }
    }
    // The panel's surface takes the pointer only over the dock (ShellView's inputRects): the
    // desktop beside it, and the room above it for an icon to bounce in, stay reachable.
    Binding {
        target: root.shellView
        property: "inputRects"
        value: root.dock ? [root.dock.area] : []
    }

    // The menu bar of the macOS style, along the output's top edge in a surface of its own
    // (MenuBarWindow in view.hpp), there while the style is macOS.
    MenuBarWindow {
        id: menuBarWindow
        panel: root.shellView
        shown: root.macos
        barHeight: Theme.menuBarHeight
        Loader {
            id: menuBarLoader
            anchors.fill: parent
            active: root.macos
            sourceComponent: Component { TopMenuBar { panel: root } }
        }
    }
}
