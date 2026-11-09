import QtQuick
import Ask

// The chips under the input. Everything shown here comes from outside (a
// selection, a window title, a file name), so it is rendered as plain text.
Flow {
    id: root
    // Called with the chip's row to toggle it, or to remove it.
    signal toggled(int row)
    signal removed(int row)

    spacing: Palette.unit

    Repeater {
        model: backend.attachments
        delegate: Rectangle {
            id: chip
            required property int index
            required property int kind
            required property string label
            required property string tooltip
            required property bool checked
            required property int phase

            readonly property bool failed: phase === 2
            readonly property bool capturing: phase === 1
            readonly property bool on: checked && !failed
            readonly property string glyph: kind === 0 ? "❝" : kind === 1 ? "▣" : "▤"
            readonly property string text: {
                if (kind === 1) {
                    const base = label.length ? qsTr("%1 window").arg(label) : qsTr("Active window")
                    if (failed) return qsTr("%1: capture failed").arg(base)
                    if (capturing) return qsTr("%1: capturing…").arg(base)
                    return base
                }
                return label
            }

            height: 26
            width: row.implicitWidth + Palette.unit * 1.5
            radius: Math.min(6, theme.radius)
            color: on ? Qt.alpha(theme.accent, 0.13) : "transparent"
            border.color: failed ? Palette.danger : on ? Qt.alpha(theme.accent, 0.4) : Palette.border
            opacity: on || failed || hover.hovered ? 1 : 0.75

            Behavior on color { ColorAnimation { duration: Palette.quick } }

            Row {
                id: row
                anchors.verticalCenter: parent.verticalCenter
                x: Palette.unit
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.glyph
                    textFormat: Text.PlainText
                    color: chip.on ? theme.accent : Palette.faint
                    font.pixelSize: 12
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(120, root.width * 0.6) - Palette.unit * 7)
                    text: chip.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: chip.failed ? Palette.danger : chip.on ? Palette.text : Palette.dim
                    font.pixelSize: 12
                }
                KeyCap {
                    visible: chip.kind === 1 && !chip.on && !chip.capturing
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Ctrl S"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    textFormat: Text.PlainText
                    color: closeHover.hovered ? Palette.text : Palette.faint
                    font.pixelSize: 14
                    leftPadding: 2
                    rightPadding: 2
                    HoverHandler { id: closeHover }
                    TapHandler { onTapped: root.removed(chip.index) }
                }
            }

            HoverHandler { id: hover }
            TapHandler { onTapped: root.toggled(chip.index) }

            Rectangle {
                id: tip
                visible: hover.hovered && chip.tooltip.length > 0 && tipDelay.done
                z: 20
                y: chip.height + 4
                width: Math.min(tipText.implicitWidth + Palette.unit * 2, 360)
                height: tipText.implicitHeight + Palette.unit
                x: Math.min(0, root.parent.width - Palette.unit * 2 - chip.mapToItem(root.parent, 0, 0).x - width)
                radius: Math.min(6, theme.radius)
                color: "#202023"
                border.color: Palette.border
                Text {
                    id: tipText
                    anchors.fill: parent
                    anchors.margins: Palette.unit / 2
                    anchors.leftMargin: Palette.unit
                    text: chip.tooltip
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 6
                    elide: Text.ElideRight
                    color: Palette.dim
                    font.pixelSize: 12
                }
            }
            Timer {
                id: tipDelay
                property bool done: false
                interval: 500
                running: hover.hovered
                onTriggered: done = true
                onRunningChanged: if (!running) done = false
            }
        }
    }
}
