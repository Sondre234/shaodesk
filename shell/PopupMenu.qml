// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A menu on popup cards, with submenus that cascade beside their entries. Its entries are plain
// objects, so that a menu is written as a list:
//
//   { text, icon, secondary, toggle, checked, enabled, danger, run, submenu, objectName }
//   { separator: true }     a line between groups
//   { header: "Name" }      a section's heading
//
// - text: the label. icon: a line icon Icon.qml draws ("power"), else a theme icon's name
//   ("window-new") or an image's URL. secondary: muted text at the end of the row, such as a
//   shortcut or the value in use.
// - toggle: "check" (or "checkmark") or "radio", with checked whether it is on.
// - enabled: false greys it out. danger: true draws it in the danger colour, for an entry that
//   ends or destroys something.
// - run: called when the entry is chosen. The menu then asks to be closed (dismissed()), unless
//   run returns true. Choosing an entry can change the entries: run must not count on the menu
//   or the row that called it afterwards.
// - submenu: the entries shown beside this one, or a function returning them, read as the
//   submenu opens and again whenever what it read changes.
// - objectName: its row's, for tests; else `entryName` (`separatorName` for a separator).
//
// The menu fills its parent, the popover's popupLayer, and places its first card as a PopupCard
// does, by `anchorRect`, `side`, `alignment` and `gap`. A submenu opens when the pointer rests on
// its entry for `submenuDelay` milliseconds, on a click, or with Right, Enter or Space; it opens
// on the side its parent opened towards, or the other where there is no room, and its parent
// stays open. Up and Down (wrapping), Home and End move through the entries the keyboard is in,
// skipping what cannot be chosen; Enter or Space chooses; Left or Escape closes a submenu; Escape
// in the first level asks to close. Whoever uses it sets `open` and clears it on dismissed().
Item {
    id: popupMenu
    property bool open: false
    property var entries: []
    property rect anchorRect
    property int side: Qt.TopEdge
    property int alignment: Qt.AlignLeft
    property real gap: Theme.spacingM
    // Where its cards stay: its parent's rectangle, unless set (panel.popupArea for a bar's).
    property rect bounds: Qt.rect(0, 0, width, height)
    property real minimumWidth: 200
    property real maximumWidth: 420
    property real rowHeight: 36
    // The entry highlighted as it opens: -1 for none, as when it is opened with the pointer.
    property int initialIndex: -1
    property int submenuDelay: 250
    property string entryName: "menuEntry"
    property string separatorName: "menuSeparator"
    // The first level's card.
    readonly property Item card: top
    // The entries whose submenus are open, outermost first. Read-only.
    property var openEntries: []
    // What makes a submenu's level.
    readonly property Component levelComponent: levelMaker
    // An entry was chosen (after its run), and the menu wants to be closed.
    signal triggered(var entry)
    signal dismissed()

    anchors.fill: parent
    // While its first card shows (whose own visible follows this one's).
    visible: top.open || top.progress > 0

    function submenuOf(entry) {
        var submenu = entry ? entry.submenu : undefined
        return typeof submenu === "function" ? submenu() : (submenu || [])
    }
    function choose(entry) {
        var keep = entry.run ? entry.run() === true : false
        triggered(entry)
        if (!keep)
            dismissed()
    }
    // The submenus open now, after one opened or closed.
    function updatePath() {
        var path = []
        for (var level = top; level.child && level.child.open; level = level.child)
            path.push(level.child.entry)
        openEntries = path
    }
    function closeSubmenus() { top.closeChild() }
    // Opens the submenu of the first level's entry `index`, as a click on it would.
    function openSubmenu(index) { top.activate(index, false) }
    function deepest() {
        var level = top
        while (level.child && level.child.open)
            level = level.child
        return level
    }
    onOpenChanged: {
        if (open) {
            top.closeChild()
            top.current = -1
            if (initialIndex >= 0) {
                top.current = initialIndex - 1
                top.move(1)
            }
        } else {
            // The submenus fade out with the first level, and are gone when it opens again.
            for (var level = top.child; level; level = level.child)
                level.open = false
            top.stopResting()
            updatePath()
        }
    }

    Keys.onPressed: (event) => {
        var level = deepest()
        switch (event.key) {
        case Qt.Key_Up: level.move(-1); break
        case Qt.Key_Down: level.move(1); break
        case Qt.Key_Home: level.current = -1; level.move(1); break
        case Qt.Key_End: level.current = -1; level.move(-1); break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (level.current >= 0)
                level.activate(level.current, true)
            break
        case Qt.Key_Right:
            if (level.current >= 0 && level.hasSubmenu(level.current))
                level.openChild(level.current, true)
            break
        case Qt.Key_Left:
            if (level.parentLevel)
                level.parentLevel.closeChild()
            break
        case Qt.Key_Escape:
            if (level.parentLevel)
                level.parentLevel.closeChild()
            else
                popupMenu.dismissed()
            break
        default:
            return
        }
        event.accepted = true
    }

    // One level: the first, or a submenu.
    component Level: PopupCard {
        id: level
        required property Item menu
        property Item parentLevel: null
        property var entry: null // the entry it is the submenu of
        property int depth: 0
        property var entries: []
        // The row highlighted, and the one whose submenu is open; -1 for none.
        property int current: -1
        property int openIndex: -1
        property Item child: null
        readonly property real padding: Theme.spacingS

        initialFocus: null
        onOpened: level.menu.forceActiveFocus()

        function selectable(index) {
            var e = entries[index]
            return !!e && !e.separator && !e.header && e.enabled !== false
        }
        function hasSubmenu(index) { return selectable(index) && !!entries[index].submenu }
        function move(step) {
            var count = entries.length
            var index = current
            for (var tried = 0; tried < count; ++tried) {
                index = index < 0 ? (step > 0 ? 0 : count - 1) : (index + step + count) % count
                if (selectable(index)) {
                    current = index
                    rows.positionViewAtIndex(index, ListView.Contain)
                    return
                }
            }
        }
        function activate(index, byKeyboard) {
            if (!selectable(index))
                return
            current = index
            if (hasSubmenu(index))
                openChild(index, byKeyboard)
            else
                level.menu.choose(entries[index])
        }
        function openChild(index, byKeyboard) {
            stopResting()
            if (child && openIndex === index) {
                if (byKeyboard && child.current < 0)
                    child.move(1)
                return
            }
            closeChild()
            var row = rows.itemAtIndex(index)
            if (!row)
                return
            // Beside this card, its first row level with the entry, from where the card rests
            // (not where it is sliding in from).
            var at = row.mapToItem(level, 0, 0)
            openIndex = index
            current = index
            child = level.menu.levelComponent.createObject(level.menu, {
                menu: level.menu, parentLevel: level, entry: entries[index], depth: depth + 1,
                anchorRect: Qt.rect(level.x, level.y + at.y - padding, level.width, row.height + 2 * padding),
                side: depth === 0 ? Qt.RightEdge : placedSide, alignment: Qt.AlignTop,
                gap: Theme.spacingXS, bounds: bounds, open: true
            })
            child.entries = Qt.binding(function() { return level.menu.submenuOf(level.entries[level.openIndex]) })
            if (byKeyboard)
                child.move(1)
            level.menu.updatePath()
        }
        function closeChild() {
            if (!child)
                return
            child.closeChild()
            var gone = child
            child = null
            openIndex = -1
            gone.destroy()
            level.menu.updatePath()
        }
        // The pointer on row `index`: it is highlighted at once, and its submenu opens (or the
        // one open closes) once the pointer has rested there.
        function rowHovered(index) {
            current = selectable(index) ? index : -1
            resting.restart()
            if (parentLevel)
                parentLevel.keepChild()
        }
        function stopResting() { resting.stop() }
        // The pointer reached the submenu: what it crossed on the way does not count.
        function keepChild() {
            resting.stop()
            if (openIndex >= 0)
                current = openIndex
            if (parentLevel)
                parentLevel.keepChild()
        }
        Timer {
            id: resting
            interval: level.menu.submenuDelay
            onTriggered: {
                if (level.current === level.openIndex)
                    return
                if (level.current >= 0 && level.hasSubmenu(level.current))
                    level.openChild(level.current, false)
                else
                    level.closeChild()
            }
        }
        // What changed under an open menu (an application's own) may have moved its rows.
        onEntriesChanged: {
            if (current >= entries.length || (current >= 0 && !selectable(current)))
                current = -1
            if (openIndex >= 0 && !hasSubmenu(openIndex))
                closeChild()
        }
        HoverHandler {
            onHoveredChanged: {
                if (hovered && level.parentLevel)
                    level.parentLevel.keepChild()
                else if (!hovered && !level.child)
                    level.current = -1
            }
        }

        FontMetrics { id: body; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
        FontMetrics { id: small; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily }
        readonly property bool iconColumn: entries.some(function(e) { return !e.separator && !e.header && (e.icon || e.toggle) })
        // As wide as its widest row wants, within the menu's bounds.
        readonly property real naturalWidth: {
            var label = 0, secondary = 0, submenu = false
            for (var i = 0; i < entries.length; ++i) {
                var e = entries[i]
                if (e.separator)
                    continue
                if (e.header) {
                    label = Math.max(label, small.advanceWidth(e.header))
                    continue
                }
                label = Math.max(label, body.advanceWidth(e.text || ""))
                if (e.secondary)
                    secondary = Math.max(secondary, small.advanceWidth(e.secondary))
                submenu = submenu || !!e.submenu
            }
            return Theme.spacingL + (iconColumn ? Theme.iconSizeSmall + Theme.spacingL : 0) + label +
                   (secondary > 0 ? Theme.spacingL + secondary : 0) +
                   (submenu ? Theme.spacingL + Theme.iconSizeSmall : 0) + Theme.spacingM + 2
        }
        readonly property real naturalHeight: {
            var sum = 0
            for (var i = 0; i < entries.length; ++i)
                sum += entries[i].separator ? 9 : entries[i].header ? 30 : level.menu.rowHeight
            return Math.max(level.menu.rowHeight, sum)
        }
        implicitWidth: Math.min(level.menu.maximumWidth, Math.max(level.menu.minimumWidth, naturalWidth + 2 * padding))
        implicitHeight: naturalHeight + 2 * padding

        ListView {
            id: rows
            anchors.fill: parent
            anchors.margins: level.padding
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            model: level.entries
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: MenuRow {
                width: ListView.view.width
                rowHeight: level.menu.rowHeight
                iconColumn: level.iconColumn
                objectName: modelData.objectName || (modelData.separator ? level.menu.separatorName : level.menu.entryName)
                highlighted: level.current === index
                expanded: level.openIndex === index
                onHoveredChanged: if (hovered) level.rowHovered(index)
                onClicked: level.activate(index, false)
            }
        }
    }
    Component {
        id: levelMaker
        Level {}
    }

    Level {
        id: top
        menu: popupMenu
        entries: popupMenu.entries
        open: popupMenu.open
        anchorRect: popupMenu.anchorRect
        side: popupMenu.side
        alignment: popupMenu.alignment
        gap: popupMenu.gap
        bounds: popupMenu.bounds
    }
}
