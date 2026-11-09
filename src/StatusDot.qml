import QtQuick
import Ask

Rectangle {
    width: 6
    height: 6
    radius: 3
    color: theme.accent

    SequentialAnimation on opacity {
        running: parent !== null && visible
        loops: Animation.Infinite
        NumberAnimation { from: 1; to: 0.3; duration: 520; easing.type: Easing.OutQuad }
        NumberAnimation { from: 0.3; to: 1; duration: 520; easing.type: Easing.OutQuad }
    }
}
