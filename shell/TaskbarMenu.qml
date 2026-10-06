// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The context menu of a window's button (its pinned application's too), of a pinned
// application's button, or of the bar itself, whose appearance entry lists the profiles in its
// place.
Rectangle {
    id: contextMenu
    required property var panel
    required property Item barItem
    parent: panel
    objectName: "contextMenu"
    readonly property var actions: panel.taskMenuId >= 0
        ? [{ text: "Maximize / restore", run: function(id) { shell.tasks.maximize(id) } },
           { text: "Minimize", run: function(id) { shell.tasks.minimize(id) } }]
          .concat(panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : [])
          .concat([{ text: "Close window", run: function(id) { shell.tasks.close(id) } }])
        : panel.pinMenuApp !== null
        ? [{ text: "Open " + panel.pinMenuApp.name, run: function() { shell.launch(panel.pinMenuApp.appId) } }]
          .concat(panel.pinMenuApp.configured ? [] : [panel.pinAction(panel.pinMenuApp.appId)])
        : panel.profileMenu
        ? [{ text: "‹ Back", run: function() { panel.profileMenu = false; return true } }]
          .concat(shell.profiles.map(function(name) {
              return { text: (name === shell.profile ? "✓ " : "") + name,
                       run: function() { shell.pickProfile(name) } } }))
        : [{ text: panel.tiling ? "Turn tiling off" : "Turn tiling on", enabled: shell.tilingAvailable,
             run: function() { shell.toggleTiling(contextMenu.panel.outputName) } },
           { text: "Applications", run: function() { panel.launcherOpen = true } },
           { text: "Show desktop", run: function() { shell.tasks.showDesktop() } }]
          .concat(shell.profiles.length > 0
              ? [{ text: "Appearance: " + (shell.profile || "none") + " …",
                   run: function() { panel.profileMenu = true; return true } }]
              : [])
    visible: panel.taskMenuId >= 0 || panel.pinMenuApp !== null || panel.barMenuOpen
    width: 220; height: 12 + actions.length * 44 + (actions.length - 1) * 2
    x: Math.max(8, Math.min(panel.contextMenuX, panel.width - width - 8))
    y: panel.onTop ? barItem.y + barItem.height + 8 : barItem.y - height - 8
    color: Theme.surface; radius: Theme.radiusMedium
    border.color: Theme.border
    MouseArea { anchors.fill: parent }
    Column {
        anchors.fill: parent; anchors.margins: 6; spacing: 2
        Repeater {
            model: contextMenu.actions
            delegate: FlatButton {
                required property var modelData
                objectName: "contextMenuItem"
                width: parent.width; height: 44
                text: modelData.text
                enabled: modelData.enabled !== false
                opacity: enabled ? 1 : 0.4
                palette.buttonText: Theme.text
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                // Run before closing, so opening the launcher keeps the surface expanded.
                onClicked: {
                    // Running an entry can rebuild the entries, destroying this button.
                    var owner = contextMenu.panel
                    // An entry that leads to more entries returns true to stay open.
                    if (modelData.run(owner.taskMenuId) === true)
                        return
                    owner.taskMenuId = -1; owner.pinMenuApp = null; owner.barMenuOpen = false
                }
            }
        }
    }
}
