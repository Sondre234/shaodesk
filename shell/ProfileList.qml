// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The profile button's popup: the appearance profiles, the one in use marked.
PopupMenu {
    id: profileList
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "profileList"
    entryName: "profileItem"
    open: panel.audioPopup === "profiles"
    anchorRect: panel.barAnchor(panel.audioPopupX, 0)
    side: panel.popupSide
    alignment: Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 240
    onDismissed: panel.audioPopup = ""
    entries: [{ header: "Appearance" }].concat(shell.profiles.map(function(name) {
        return { text: name, toggle: "radio", checked: name === shell.profile,
                 run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
    }))
}
