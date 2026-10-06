// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A volume, 0 to 100, greyed while what it sets is muted.
Slider {
    id: slider
    property bool muted: false
    from: 0; to: 100; stepSize: 1
    background: Rectangle {
        x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth; height: 4; radius: 2
        color: Qt.lighter(shell.panelColor, 1.8)
        Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: slider.muted ? "#627084" : shell.accent }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7
        color: slider.muted ? "#8a96a8" : (slider.pressed || slider.hovered ? Qt.lighter(shell.accent, 1.15) : shell.accent)
    }
}
