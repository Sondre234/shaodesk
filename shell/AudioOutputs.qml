// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The volume control's other popup: the outputs to play through, the one in use marked.
PopupMenu {
    id: audioOutputs
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "audioOutputs"
    entryName: "audioOutputItem"
    open: panel.audioPopup === "outputs"
    anchorRect: panel.barAnchor(panel.audioPopupX, 0)
    side: panel.popupSide
    alignment: Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 240
    onDismissed: panel.audioPopup = ""
    entries: [{ header: "Output" }].concat(panel.audioSource.outputs.map(function(output) {
        return { text: output.description, toggle: "radio", checked: output.name === audioOutputs.panel.audioSource.output,
                 run: function() { audioOutputs.panel.audioSource.setOutput(output.name) } }
    }))
}
