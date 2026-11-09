import QtQuick
import Ask

Rectangle {
    id: root
    property alias text: label.text
    implicitWidth: Math.max(18, label.implicitWidth + 10)
    implicitHeight: 18
    radius: Math.min(4, theme.radius)
    color: Palette.keycap

    Text {
        id: label
        anchors.centerIn: parent
        color: Palette.dim
        font.pixelSize: 11
    }
}
