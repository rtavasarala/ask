import QtQuick
import Ask
import QtQuick.Controls.Basic

// Raycast-style actions popover: type to filter, arrows to move, Enter to run.
Item {
    id: root
    // [{id, label, keys}]
    property var actions: []
    signal triggered(string id)
    signal dismissed

    property string query: ""
    property int current: 0
    readonly property var shown: actions.filter(a => a.label.toLowerCase().includes(query.toLowerCase()))
    readonly property int rowHeight: 32

    visible: false

    function open() {
        query = ""
        current = 0
        visible = true
        filter.forceActiveFocus()
    }
    function close() {
        visible = false
    }

    onVisibleChanged: if (!visible) dismissed()

    MouseArea {
        anchors.fill: parent
        enabled: root.visible
        onClicked: root.close()
    }

    Item {
        id: popover
        width: 280
        height: list.height + filterRow.height + 1 + Palette.unit
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Palette.unit * 2
        anchors.bottomMargin: 48
        opacity: root.visible ? 1 : 0
        transform: Translate { y: root.visible ? 0 : 4; Behavior on y { NumberAnimation { duration: Palette.quick; easing.type: Easing.OutCubic } } }
        Behavior on opacity { NumberAnimation { duration: Palette.quick; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            anchors.topMargin: 1
            anchors.bottomMargin: -5
            radius: theme.radius + 1
            color: "#66000000"
        }
        Rectangle {
            anchors.fill: parent
            radius: theme.radius
            color: "#202023"
            border.color: Palette.border
        }

        ListView {
            id: list
            x: Palette.unit / 2
            y: Palette.unit / 2
            width: parent.width - Palette.unit
            height: Math.max(1, count) * root.rowHeight
            interactive: false
            model: root.shown
            currentIndex: root.current
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                height: root.rowHeight
                radius: Math.min(6, theme.radius)
                color: index === root.current ? Palette.selected : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Palette.unit
                    text: modelData.label
                    color: Palette.text
                    font.pixelSize: 13
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Palette.unit
                    spacing: 4
                    Repeater {
                        model: modelData.keys
                        KeyCap { required property string modelData; text: modelData }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.current = index
                    onClicked: root.triggered(modelData.id)
                }
            }
        }

        Rectangle {
            id: sep
            y: list.y + list.height + Palette.unit / 2
            width: parent.width
            height: 1
            color: Palette.divider
        }

        Item {
            id: filterRow
            y: sep.y + 1
            width: parent.width
            height: 32
            TextInput {
                id: filter
                anchors.fill: parent
                anchors.leftMargin: Palette.unit * 1.5
                anchors.rightMargin: Palette.unit * 1.5
                verticalAlignment: TextInput.AlignVCenter
                color: Palette.text
                font.pixelSize: 13
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
                onTextChanged: { root.query = text; root.current = 0 }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        root.current = Math.min(root.shown.length - 1, root.current + 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        root.current = Math.max(0, root.current - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (root.shown.length > 0)
                            root.triggered(root.shown[root.current].id)
                        event.accepted = true
                    }
                }
                Text {
                    visible: !filter.text
                    text: qsTr("Search for actions…")
                    color: Palette.faint
                    font: filter.font
                }
            }
        }
    }
}
