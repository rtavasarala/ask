import QtQuick
import Ask
import QtQuick.Controls.Basic

// Raycast-style actions popover: type to filter, arrows to move, Enter to run.
Item {
    id: root
    // [{id, label, subtitle?, searchText?, keys, checked?, disabled?}]
    property var actions: []
    property string placeholder: qsTr("Search for actions…")
    property int popoverWidth: 310
    property bool hasSecondaryAction: false
    readonly property int maxVisible: 8
    signal triggered(string id)
    signal secondaryTriggered(string id)
    signal dismissed

    property string query: ""
    property int current: 0
    readonly property var shown: {
        const q = query.trim().toLowerCase()
        if (q.length === 0)
            return actions
        return actions.filter(a => {
            const hay = (a.label + " " + (a.subtitle || "") + " " + (a.searchText || "")).toLowerCase()
            return hay.includes(q)
        })
    }
    readonly property int rowHeight: 32

    visible: false

    function open() {
        filter.text = ""
        query = ""
        current = 0
        visible = true
        filter.forceActiveFocus()
    }
    function close() {
        visible = false
    }

    onVisibleChanged: if (!visible) dismissed()
    onCurrentChanged: list.positionViewAtIndex(current, ListView.Contain)
    onShownChanged: current = shown.length > 0 ? Math.max(0, Math.min(current, shown.length - 1)) : 0

    MouseArea {
        anchors.fill: parent
        enabled: root.visible
        onClicked: root.close()
    }

    Item {
        id: popover
        width: Math.min(root.popoverWidth, root.width - Palette.unit * 4)
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
            color: Palette.shadow
        }
        Rectangle {
            anchors.fill: parent
            radius: theme.radius
            color: Palette.popover
            border.color: Palette.border
        }

        ListView {
            id: list
            x: Palette.unit / 2
            y: Palette.unit / 2
            width: parent.width - Palette.unit
            height: Math.max(1, Math.min(count, root.maxVisible)) * root.rowHeight
            interactive: count > root.maxVisible
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            model: root.shown
            currentIndex: root.current
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                implicitWidth: 4
                contentItem: Rectangle { implicitWidth: 3; radius: 1.5; color: Palette.faint; opacity: parent.active ? 0.8 : 0.35 }
            }
            delegate: Rectangle {
                required property var modelData
                required property int index
                readonly property bool isDisabled: modelData.disabled === true
                width: list.width
                height: root.rowHeight
                radius: Math.min(6, theme.radius)
                color: index === root.current && !isDisabled ? Palette.selected : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Palette.unit
                    width: Math.max(40, parent.width - Palette.unit * 2 - (rightRow.width > 0 ? rightRow.width + Palette.unit : 0))
                    text: modelData.label
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: parent.isDisabled ? Palette.faint : Palette.text
                    font.pixelSize: 13
                }
                Row {
                    id: rightRow
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Palette.unit
                    spacing: 6

                    Text {
                        visible: Boolean(modelData.subtitle && modelData.subtitle.length > 0)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, Math.floor(list.width * 0.42))
                        text: modelData.subtitle || ""
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Palette.faint
                        font.pixelSize: 11
                    }
                    Text {
                        visible: modelData.checked === true
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✓"
                        color: theme.accent
                        font.pixelSize: 13
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Repeater {
                            model: modelData.keys || []
                            KeyCap { required property string modelData; text: modelData }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: if (!parent.isDisabled) root.current = index
                    onClicked: if (!parent.isDisabled) root.triggered(modelData.id)
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
            readonly property bool showSecondaryHint: root.hasSecondaryAction && root.shown.length > 0 && !root.shown[root.current].disabled

            TextInput {
                id: filter
                anchors.fill: parent
                anchors.leftMargin: Palette.unit * 1.5
                anchors.rightMargin: filterRow.showSecondaryHint ? secondaryHint.width + Palette.unit * 2 : Palette.unit * 1.5
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: Palette.text
                font.pixelSize: 13
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
                onTextChanged: { root.query = text; root.current = 0 }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        if (root.shown.length > 0)
                            root.current = Math.min(root.shown.length - 1, root.current + 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        if (root.shown.length > 0)
                            root.current = Math.max(0, root.current - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (root.shown.length > 0 && !root.shown[root.current].disabled)
                            root.triggered(root.shown[root.current].id)
                        event.accepted = true
                    } else if (root.hasSecondaryAction && (
                        ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_Backspace || event.key === Qt.Key_D)) ||
                        (event.key === Qt.Key_Delete && !(event.modifiers & Qt.ShiftModifier) && filter.cursorPosition === filter.text.length && filter.selectedText.length === 0)
                    )) {
                        if (root.shown.length > 0 && !root.shown[root.current].disabled)
                            root.secondaryTriggered(root.shown[root.current].id)
                        event.accepted = true
                    }
                }
                Text {
                    visible: !filter.text
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    elide: Text.ElideRight
                    text: root.placeholder
                    color: Palette.faint
                    font: filter.font
                }
            }

            Row {
                id: secondaryHint
                visible: filterRow.showSecondaryHint
                anchors.right: parent.right
                anchors.rightMargin: Palette.unit * 1.25
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Delete")
                    color: Palette.faint
                    font.pixelSize: 11
                }
                KeyCap {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Del"
                }
            }
        }
    }
}
