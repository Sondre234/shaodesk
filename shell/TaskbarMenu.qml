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

    entries: {
        var task = panel.taskMenuId
        if (task >= 0) {
            var window = menuWindows.windows[0] || { appId: "", title: "" }
            return [titleEntry(appRecord(window.appId), window.appId, window.title), { separator: true },
                    { text: "Maximize / restore", run: function() { shell.tasks.maximize(task) } },
                    { text: "Minimize", run: function() { shell.tasks.minimize(task) } }]
                .concat(panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : [])
                .concat([{ text: "Close window", run: function() { shell.tasks.close(task) } }])
        }
        var app = panel.pinMenuApp
        if (app !== null)
            return [titleEntry(app, app.appId, ""), { separator: true },
                    { text: "Open " + app.name, run: function() { shell.launch(app.appId) } }]
                .concat(app.configured ? [] : [panel.pinAction(app.appId)])
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
