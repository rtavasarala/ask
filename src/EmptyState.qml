import QtQuick
import Ask

// Quiet hint with a few prompts or AI commands to pick with the arrow keys or the mouse.
Item {
    id: root
    property var items: []
    property string hint: ""
    property int selected: -1
    signal picked(int index)

    onSelectedChanged: if (selected >= 0) itemList.positionViewAtIndex(selected, ListView.Contain)

    Column {
        id: box
        anchors.centerIn: parent
        width: Math.min(parent.width - Palette.unit * 6, 420)
        spacing: 2

        Text {
            id: glyph
            visible: root.items.length <= 4
            anchors.horizontalCenter: parent.horizontalCenter
            text: "✦"
            color: Palette.faint
            font.pixelSize: 28
        }
        Text {
            id: hintLabel
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: root.hint
            color: Palette.dim
            font.pixelSize: 13
            topPadding: root.items.length <= 4 ? Palette.unit : 0
            bottomPadding: Palette.unit
        }
        ListView {
            id: itemList
            width: parent.width
            height: Math.min(contentHeight, Math.max(64, root.height - (glyph.visible ? glyph.height + box.spacing : 0) - hintLabel.height - Palette.unit * 3))
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            spacing: 2
            model: root.items
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: itemList.width
                height: 30
                radius: Math.min(6, theme.radius)
                color: index === root.selected ? Palette.selected : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Palette.unit * 1.5
                    width: parent.width - Palette.unit * 3
                    text: modelData
                    textFormat: Text.PlainText
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
