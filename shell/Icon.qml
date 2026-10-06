// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// A line icon from Lucide (https://lucide.dev, ISC licence), drawn as a vector in any colour so it
// stays crisp at every scale and follows the panel's text colour. The paths are Lucide's own, on
// its 24-unit grid with a 2-unit round stroke; rectangles are written out as paths.
Item {
    id: icon
    property string name
    property color color: Theme.text
    property real size: 18
    width: size; height: size
    // Whether `name` is one of the icons below.
    readonly property bool known: paths[name] !== undefined

    readonly property var paths: ({
        "check": "M20 6L9 17l-5-5",
        "chevron-right": "M9 18l6-6-6-6",
        "bell": "M10.268 21a2 2 0 0 0 3.464 0 M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326",
        "bell-off": "M10.268 21a2 2 0 0 0 3.464 0 M17 17H4a1 1 0 0 1-.74-1.673C4.59 13.956 6 12.499 6 8a6 6 0 0 1 .258-1.742 M2 2l20 20 M8.668 3.01A6 6 0 0 1 18 8c0 2.687.77 4.653 1.707 6.05",
        "layout-panel-left": "M4 3h5a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z M15 3h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z M15 14h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1z",
        "copy": "M10 8h10a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H10a2 2 0 0 1-2-2V10a2 2 0 0 1 2-2z M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2",
        "power": "M12 2v10 M18.4 6.6a9 9 0 1 1-12.77.04",
        "image": "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M7 9a2 2 0 1 0 4 0a2 2 0 1 0-4 0 M21 15l-3.086-3.086a2 2 0 0 0-2.828 0L6 21",
        "search": "M3 11a8 8 0 1 0 16 0a8 8 0 1 0-16 0 M21 21l-4.34-4.34",
        "x": "M18 6L6 18 M6 6l12 12",
        "shuffle": "M18 14l4 4-4 4 M18 2l4 4-4 4 M2 18h1.973a4 4 0 0 0 3.3-1.7l5.454-8.6a4 4 0 0 1 3.3-1.7H22 M2 6h1.972a4 4 0 0 1 3.3 1.7l.553.874 M22 18h-6.041a4 4 0 0 1-3.3-1.8l-.359-.45",
        "wifi": "M12 20h.01 M2 8.82a15 15 0 0 1 20 0 M5 12.859a10 10 0 0 1 14 0 M8.5 16.429a5 5 0 0 1 7 0",
        "wifi-off": "M12 20h.01 M8.5 16.429a5 5 0 0 1 7 0 M5 12.859a10 10 0 0 1 5.17-2.69 M19 12.859a10 10 0 0 0-2.007-1.523 M2 8.82a15 15 0 0 1 4.177-2.643 M22 8.82a15 15 0 0 0-11.288-3.764 M2 2l20 20",
        "ethernet-port": "M10 8v1 M14 8v1 M18 8v1 M6 8v1 M19 17a2 2 0 0 0-1.765 1.059l-.47.882A2 2 0 0 1 15 20H9a2 2 0 0 1-1.765-1.059l-.47-.882A2 2 0 0 0 5 17H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2z",
        "volume": "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z",
        "volume-1": "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z M16 9a5 5 0 0 1 0 6",
        "volume-2": "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z M16 9a5 5 0 0 1 0 6 M19.364 18.364a9 9 0 0 0 0-12.728",
        "volume-x": "M11 4.702a.7.7 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.7.7 0 0 0 11 19.298z M16.5 14.5l5-5 M16.5 9.5l5 5"
    })

    Shape {
        width: 24; height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.color
            strokeWidth: 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
