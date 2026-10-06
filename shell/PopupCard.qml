// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// A popup's card: Theme's opaque surface with an outline and rounded corners, and a shadow under
// it when the GPU draws (Theme.effects). Setting `open` fades it in with a few pixels' slide from
// the side it opens from; clearing it fades it out the same way, and it stays visible until it
// has. Both are instant with animations off. What it holds goes inside it, filling it, and the
// item `initialFocus` takes the keyboard when it opens. Presses on it stay with it, so that they
// do not close it.
//
// It places itself beside `anchorRect`, a rectangle in its parent's coordinates (a bar item's,
// from panel.barAnchor): on the anchor's `side` (Qt.TopEdge above it, Qt.BottomEdge below it,
// Qt.LeftEdge or Qt.RightEdge beside it), `gap` away, on the other side instead when that one has
// more room for it, and at least `margin` inside its parent. `alignment` lines it up with the
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

    readonly property bool vertical: side === Qt.TopEdge || side === Qt.BottomEdge
    readonly property real areaWidth: parent ? parent.width : 0
    readonly property real areaHeight: parent ? parent.height : 0
    // The room on each side of the anchor.
    readonly property real roomAbove: anchorRect.y - gap - margin
    readonly property real roomBelow: areaHeight - anchorRect.y - anchorRect.height - gap - margin
    readonly property real roomLeft: anchorRect.x - gap - margin
    readonly property real roomRight: areaWidth - anchorRect.x - anchorRect.width - gap - margin
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
    readonly property real availableWidth: vertical ? areaWidth - 2 * margin : room(placedSide)
    readonly property real availableHeight: vertical ? room(placedSide) : areaHeight - 2 * margin
    width: Math.max(0, Math.min(implicitWidth, availableWidth))
    height: Math.max(0, Math.min(implicitHeight, availableHeight))

    // Where alignment puts it along the anchor, from `start` (the anchor's start) and `length`
    // (its length) and the card's own `size`, kept inside an area `extent` long.
    function along(start, length, size, extent) {
        var at = alignment & (Qt.AlignLeft | Qt.AlignTop) ? start
               : alignment & (Qt.AlignRight | Qt.AlignBottom) ? start + length - size
               : start + length / 2 - size / 2
        return Math.max(margin, Math.min(at, extent - margin - size))
    }
    Binding {
        when: card.anchored
        card.x: card.vertical
            ? card.along(card.anchorRect.x, card.anchorRect.width, card.width, card.areaWidth)
            : card.placedSide === Qt.LeftEdge ? card.anchorRect.x - card.gap - card.width
            : card.anchorRect.x + card.anchorRect.width + card.gap
        card.y: !card.vertical
            ? card.along(card.anchorRect.y, card.anchorRect.height, card.height, card.areaHeight)
            : card.placedSide === Qt.TopEdge ? card.anchorRect.y - card.gap - card.height
            : card.anchorRect.y + card.anchorRect.height + card.gap
    }

    visible: open || progress > 0
    opacity: progress
    onVisibleChanged: if (visible && open && initialFocus) initialFocus.forceActiveFocus()
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
        acceptedButtons: Qt.AllButtons
    }
    Item {
        id: body
        anchors.fill: parent
    }
}
