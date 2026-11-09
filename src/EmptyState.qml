import QtQuick
import Ask

// Quiet first-run hint with a few prompts to pick with the arrow keys or the mouse.
Item {
    id: root
    property var examples: []
    property int selected: -1
    signal picked(int index)

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - Palette.unit * 6, 420)
        spacing: Palette.unit

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "✦"
            color: Palette.faint
            font.pixelSize: 28
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Ask a question and the answer streams in here.")
            color: Palette.dim
            font.pixelSize: 13
            bottomPadding: Palette.unit
        }
        Repeater {
            model: root.examples
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: parent.width
                height: 32
                radius: Math.min(6, theme.radius)
                color: index === root.selected ? Palette.selected : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Palette.unit * 1.5
                    width: parent.width - Palette.unit * 3
                    text: modelData
                    color: Palette.dim
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.selected = index
                    onClicked: root.picked(index)
                }
            }
        }
    }
}
