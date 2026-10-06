// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// A popup's card: Theme's opaque surface with an outline and rounded corners, and a shadow under
// it when the GPU draws (Theme.effects). Setting `open` fades it in with a few pixels' slide from
// the side it opens from; clearing it fades it out the same way, and it stays visible until it
// has. Both are instant with animations off. What it holds goes inside it, filling it. As it
// opens it emits opened(), where what it shows is reset, and `initialFocus` (the card itself
// unless set; null for none) takes the keyboard. Presses on it stay with it, so that they do not
// close it, until it starts closing: a press then goes to what is under it.
//
// It places itself beside `anchorRect`, a rectangle in its parent's coordinates (a bar item's,
// from panel.barAnchor): on the anchor's `side` (Qt.TopEdge above it, Qt.BottomEdge below it,
// Qt.LeftEdge or Qt.RightEdge beside it), `gap` away, on the other side instead when that one has
// more room for it, and at least `margin` inside `bounds` (its parent's rectangle, unless set; a
// bar's popup keeps off the bar with panel.popupArea). `alignment` lines it up with the
// anchor along that side: Qt.AlignHCenter or Qt.AlignVCenter centres it, Qt.AlignLeft or
// Qt.AlignTop starts it at the anchor's left or top edge, Qt.AlignRight or Qt.AlignBottom ends it
// at the other. Its size is its implicit size, cut down to the room there is (availableWidth and
// availableHeight), so set implicitWidth and implicitHeight rather than width and height. With
// `anchored` false it leaves its place to whoever uses it.
Item {
    id: card
    property bool open: false
    property rect anchorRect
    property int side: Qt.TopEdge
    property int alignment: Qt.AlignHCenter
    property real gap: Theme.spacingM
    property real margin: Theme.spacingM
    property bool anchored: true
    property color color: Theme.surface
    property real radius: Theme.radiusMedium
    property Item initialFocus: card
    default property alias content: body.data
    // How far it is open, from 0 to 1: its opacity, and what is left of the slide.
    property real progress: 0

    property rect bounds: parent ? Qt.rect(0, 0, parent.width, parent.height) : Qt.rect(0, 0, 0, 0)

    readonly property bool vertical: side === Qt.TopEdge || side === Qt.BottomEdge
    // The room on each side of the anchor.
    readonly property real roomAbove: anchorRect.y - gap - margin - bounds.y
    readonly property real roomBelow: bounds.y + bounds.height - anchorRect.y - anchorRect.height - gap - margin
    readonly property real roomLeft: anchorRect.x - gap - margin - bounds.x
    readonly property real roomRight: bounds.x + bounds.width - anchorRect.x - anchorRect.width - gap - margin
    function room(edge) {
        return edge === Qt.TopEdge ? roomAbove : edge === Qt.BottomEdge ? roomBelow
             : edge === Qt.LeftEdge ? roomLeft : roomRight
    }
    function opposite(edge) {
        return edge === Qt.TopEdge ? Qt.BottomEdge : edge === Qt.BottomEdge ? Qt.TopEdge
             : edge === Qt.LeftEdge ? Qt.RightEdge : Qt.LeftEdge
    }
    // The side it opens on: `side`, unless it does not fit there and the other side has more room.
    readonly property int placedSide: {
        var wanted = vertical ? implicitHeight : implicitWidth
        return wanted > room(side) && room(opposite(side)) > room(side) ? opposite(side) : side
    }
    readonly property real availableWidth: vertical ? bounds.width - 2 * margin : room(placedSide)
    readonly property real availableHeight: vertical ? room(placedSide) : bounds.height - 2 * margin
    width: Math.max(0, Math.min(implicitWidth, availableWidth))
    height: Math.max(0, Math.min(implicitHeight, availableHeight))

    // Where alignment puts it along the anchor, from `start` (the anchor's start) and `length`
    // (its length) and the card's own `size`, kept inside the bounds from `low` to `high`.
    function along(start, length, size, low, high) {
        var at = alignment & (Qt.AlignLeft | Qt.AlignTop) ? start
               : alignment & (Qt.AlignRight | Qt.AlignBottom) ? start + length - size
               : start + length / 2 - size / 2
        return Math.max(low + margin, Math.min(at, high - margin - size))
    }
    Binding {
        when: card.anchored
        card.x: card.vertical
            ? card.along(card.anchorRect.x, card.anchorRect.width, card.width, card.bounds.x,
                         card.bounds.x + card.bounds.width)
            : card.placedSide === Qt.LeftEdge ? card.anchorRect.x - card.gap - card.width
            : card.anchorRect.x + card.anchorRect.width + card.gap
        card.y: !card.vertical
            ? card.along(card.anchorRect.y, card.anchorRect.height, card.height, card.bounds.y,
                         card.bounds.y + card.bounds.height)
            : card.placedSide === Qt.TopEdge ? card.anchorRect.y - card.gap - card.height
            : card.anchorRect.y + card.anchorRect.height + card.gap
    }

    visible: open || progress > 0
    opacity: progress
    // Once each time it opens, when it is visible: made open, opened, or opened again while it
    // was still fading out.
    signal opened()
    onOpened: if (initialFocus) initialFocus.forceActiveFocus()
    property bool announced: false
    function announce() {
        if (open && visible && !announced) {
            announced = true
            opened()
        }
    }
    onOpenChanged: {
        if (!open) announced = false
        announce()
    }
    onVisibleChanged: announce()
    Component.onCompleted: announce()
    states: State {
        name: "open"
        when: card.open
        PropertyChanges { card.progress: 1 }
    }
    transitions: [
        Transition {
            to: "open"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "open"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]
    // It slides out from the anchor, a few pixels.
    transform: Translate {
        readonly property real distance: (1 - card.progress) * Theme.spacingM
        x: card.placedSide === Qt.LeftEdge ? distance : card.placedSide === Qt.RightEdge ? -distance : 0
        y: card.placedSide === Qt.TopEdge ? distance : card.placedSide === Qt.BottomEdge ? -distance : 0
    }

    Loader {
        anchors.fill: parent
        active: Theme.effects
        sourceComponent: RectangularShadow {
            radius: card.radius
            blur: Theme.shadowBlur
            offset: Qt.vector2d(0, Theme.shadowOffset)
            color: Theme.shadow
        }
    }
    Rectangle {
        anchors.fill: parent
        color: card.color
        radius: card.radius
        border.color: Theme.border
    }
    MouseArea {
        anchors.fill: parent
        enabled: card.open
        acceptedButtons: Qt.AllButtons
    }
    Item {
        id: body
        anchors.fill: parent
        enabled: card.open
    }
}
