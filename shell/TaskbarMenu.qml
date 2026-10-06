// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The context menu of a window's button (its pinned application's too), of a pinned
// application's button, or of the bar itself, whose appearance entry opens the profiles beside
// it. It opens where the bar was pressed.
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
    entries: {
        var task = panel.taskMenuId
        if (task >= 0)
            return [{ text: "Maximize / restore", run: function() { shell.tasks.maximize(task) } },
                    { text: "Minimize", run: function() { shell.tasks.minimize(task) } }]
                .concat(panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : [])
                .concat([{ text: "Close window", run: function() { shell.tasks.close(task) } }])
        var app = panel.pinMenuApp
        if (app !== null)
            return [{ text: "Open " + app.name, run: function() { shell.launch(app.appId) } }]
                .concat(app.configured ? [] : [panel.pinAction(app.appId)])
        return [{ text: panel.tiling ? "Turn tiling off" : "Turn tiling on", enabled: shell.tilingAvailable,
                  run: function() { shell.toggleTiling(contextMenu.panel.outputName) } },
                { text: "Applications", run: function() { contextMenu.panel.launcherOpen = true } },
                { text: "Show desktop", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", secondary: shell.profile,
                     submenu: shell.profiles.map(function(name) {
                         return { text: name, toggle: "radio", checked: name === shell.profile,
                                  run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
                     }) }]
                : [])
    }
}
