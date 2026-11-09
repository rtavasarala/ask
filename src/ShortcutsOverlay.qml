import QtQuick
import Ask

Item {
    id: root
    signal dismissed
    property var rows: []

    Rectangle { anchors.fill: parent; color: "#99000000"; radius: theme.radius }
    MouseArea { anchors.fill: parent; onClicked: root.dismissed() }

    Rectangle {
        anchors.centerIn: parent
        width: 380
        height: column.height + Palette.unit * 4
        radius: theme.radius
        color: "#202023"
        border.color: Palette.border

        Column {
            id: column
            anchors.centerIn: parent
            width: parent.width - Palette.unit * 4
            spacing: Palette.unit
            Text {
                text: qsTr("Keyboard Shortcuts")
                color: Palette.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                bottomPadding: 4
            }
            Repeater {
                model: root.rows
                Item {
                    required property var modelData
                    width: column.width
                    height: 22
                    Text { text: modelData.label; color: Palette.dim; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    Row {
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
