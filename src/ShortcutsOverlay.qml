import QtQuick
import QtQuick.Layouts
import Ask

// Compact two-column shortcut sheet sized to fit comfortably even at 340px window height.
Item {
    id: root
    signal dismissed
    property var rows: []

    Rectangle { anchors.fill: parent; color: Palette.scrim; radius: theme.radius }
    MouseArea { anchors.fill: parent; onClicked: root.dismissed() }

    Item {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Palette.unit * 4, 600)
        height: Math.min(header.height + scroll.contentHeight + Palette.unit * 4.5, parent.height - Palette.unit * 3)

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            anchors.topMargin: 1
            anchors.bottomMargin: -5
            radius: theme.radius + 1
            color: Palette.shadow
        }
        Rectangle {
            anchors.fill: parent
            radius: theme.radius
            color: Palette.popover
            border.color: Palette.border
        }
        MouseArea { anchors.fill: parent }

        Text {
            id: header
            x: Palette.unit * 2.5
            y: Palette.unit * 1.75
            text: qsTr("Keyboard Shortcuts")
            color: Palette.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        Flickable {
            id: scroll
            x: Palette.unit * 2.5
            y: header.y + header.height + Palette.unit * 1.25
            width: parent.width - Palette.unit * 5
            height: parent.height - y - Palette.unit * 1.75
            contentWidth: width
            contentHeight: grid.implicitHeight
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            GridLayout {
                id: grid
                width: scroll.width
                columns: width >= 440 ? 2 : 1
                columnSpacing: Palette.unit * 3
                rowSpacing: 4

                Repeater {
                    model: root.rows
                    Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 22

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(40, parent.width - keysRow.width - Palette.unit)
                            text: modelData.label
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Palette.dim
                            font.pixelSize: 12
                        }
                        Row {
                            id: keysRow
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Repeater {
                                model: modelData.keys
                                KeyCap { required property string modelData; text: modelData }
                            }
                        }
                    }
                }
            }
        }
    }
}
