// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Shaodesk

// The context menu of a window's button (its pinned application's too), of a pinned
// application's button, or of the bar itself, whose appearance entry opens the profiles beside
// it. It opens where the bar was pressed. A window's and a pinned application's begin with the
// application's icon and name.
PopupMenu {
    id: contextMenu
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "contextMenu"
    entryName: "contextMenuItem"
    open: panel.taskMenuId >= 0 || panel.pinMenuApp !== null || panel.barMenuOpen
    anchorRect: panel.barAnchor(panel.contextMenuX, 0)
    side: panel.popupSide
    alignment: Qt.AlignLeft
    bounds: panel.popupArea
    // Closing only this menu: an entry may have opened the launcher.
    onDismissed: { panel.taskMenuId = -1; panel.pinMenuApp = null; panel.barMenuOpen = false }

    // The window a task menu is about, as data from the panel's windows, which follows its
    // changes while the menu is open.
    TaskFilter {
        id: menuWindows
        controller: shell
        sourceModel: contextMenu.panel.taskMenuId >= 0 ? contextMenu.panel.taskSource : null
        taskId: contextMenu.panel.taskMenuId
    }
    // The application a window belongs to, as shell.apps lists it: the one whose pinned slot it
    // takes (a configured launcher too), else the installed one; null when there is none.
    function appRecord(windowAppId) {
        var id = shell.pinnedAppFor(windowAppId) || shell.appFor(windowAppId)
        var apps = id !== "" ? shell.apps : []
        for (var i = 0; i < apps.length; ++i)
            if (apps[i].appId === id)
                return apps[i]
        return null
    }
    // The menu's title: the application's icon and name (the window's app id when it has no
    // entry), with `line` under it.
    function titleEntry(record, windowAppId, line) {
        return { title: record ? record.name : (windowAppId || line),
                 icon: record ? record.icon : shell.iconFor(windowAppId),
                 secondary: line, objectName: "contextMenuTitle" }
    }
    // What the application offers to start besides itself (its desktop actions, with their
    // icons), then `open` starting it, unless an action of its own is a new window already.
    function launchEntries(record, open) {
        if (!record)
            return []
        var actions = shell.appActions(record.appId)
        var entries = actions.map(function(action) {
            return { text: action.name, icon: action.icon, objectName: "contextMenuAction",
                     run: function() { shell.launchAction(record.appId, action.action) } }
        })
        var newWindow = actions.some(function(action) {
            return action.action === "new-window" || action.name.toLowerCase() === "new window"
        })
        return open === "New window" && newWindow ? entries
            : entries.concat([{ text: open, icon: open === "Open" ? "app-window" : "plus",
                                run: function() { shell.launch(record.appId) } }])
    }
    // The groups of entries, with a line between those that have any.
    function sections(groups) {
        var entries = []
        for (var i = 0; i < groups.length; ++i) {
            if (groups[i].length === 0)
                continue
            if (entries.length > 0)
                entries.push({ separator: true })
            entries = entries.concat(groups[i])
        }
        return entries
    }

    entries: {
        var task = panel.taskMenuId
        if (task >= 0) {
            var window = menuWindows.windows[0] || { appId: "", title: "" }
            var record = appRecord(window.appId)
            return sections([[titleEntry(record, window.appId, window.title)],
                             launchEntries(record, "New window"),
                             [{ text: "Maximize / restore", run: function() { shell.tasks.maximize(task) } },
                              { text: "Minimize", run: function() { shell.tasks.minimize(task) } }]
                                 .concat(panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : [])
                                 .concat([{ text: "Close window", run: function() { shell.tasks.close(task) } }])])
        }
        // A pinned application without windows.
        var app = panel.pinMenuApp
        if (app !== null)
            return sections([[titleEntry(app, app.appId, "")], launchEntries(app, "Open"),
                             app.configured ? [] : [panel.pinAction(app.appId)]])
        // Icons of what each entry leads to: floating windows or tiles, as the tiling button shows.
        return [{ text: panel.tiling ? "Turn tiling off" : "Turn tiling on",
                  icon: panel.tiling ? "copy" : "layout-panel-left", enabled: shell.tilingAvailable,
                  run: function() { shell.toggleTiling(contextMenu.panel.outputName) } },
                { text: "Applications", icon: "layout-grid", run: function() { contextMenu.panel.launcherOpen = true } },
                { text: "Show desktop", icon: "minimize-2", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", icon: "palette", secondary: shell.profile,
                     submenu: shell.profiles.map(function(name) {
                         return { text: name, toggle: "radio", checked: name === shell.profile,
                                  run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
                     }) }]
                : [])
    }
}
