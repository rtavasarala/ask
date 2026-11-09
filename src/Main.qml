import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ask

Window {
    id: win
    width: 750
    height: 475
    minimumWidth: 560
    minimumHeight: 340
    title: "Ask"
    // The panel draws its own corners so they follow `theme.radius`.
    color: "transparent"

    readonly property bool running: backend.status === Backend.Running
    readonly property bool hasMessages: backend.messages.count > 0
    property bool helpVisible: false
    property int exampleIndex: -1
    readonly property bool actionsOpen: actionsLoader.item !== null && actionsLoader.item.visible

    readonly property var examples: [
        qsTr("Explain how TCP slow start works"),
        qsTr("Write a regex that matches ISO 8601 dates"),
        qsTr("What is the difference between a mutex and a semaphore?")
    ]

    readonly property var actionItems: {
        const items = []
        if (running)
            items.push({id: "stop", label: qsTr("Stop Generating"), keys: ["Esc"]})
        if (backend.messages.lastHasText)
            items.push({id: "copyAnswer", label: qsTr("Copy Answer"), keys: ["Ctrl", "Shift", "C"]})
        if (hasMessages)
            items.push({id: "copyQuestion", label: qsTr("Copy Question"), keys: []})
        items.push({id: "newChat", label: qsTr("New Chat"), keys: ["Ctrl", "N"]})
        items.push({id: "shortcuts", label: qsTr("Keyboard Shortcuts"), keys: ["F1"]})
        return items
    }

    function focusInput() {
        input.forceActiveFocus()
    }

    function runAction(id) {
        if (actionsLoader.item)
            actionsLoader.item.close()
        if (id === "stop") backend.stop()
        else if (id === "copyAnswer") backend.copyLastAnswer()
        else if (id === "copyQuestion") backend.copyLastQuestion()
        else if (id === "newChat") newChat()
        else if (id === "shortcuts") helpVisible = true
    }

    function openActions() {
        actionsLoader.active = true
        actionsLoader.item.open()
    }

    function newChat() {
        backend.newChat()
        exampleIndex = -1
        input.forceActiveFocus()
    }

    function submit(text) {
        if (win.running || text.trim().length === 0)
            return
        list.follow = true
        backend.send(text)
        input.clear()
        exampleIndex = -1
    }

    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: win.actionsOpen ? actionsLoader.item.close() : win.openActions() }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: win.newChat() }
    Shortcut { sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut; onActivated: backend.copyLastAnswer() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: ["F1", "Ctrl+/"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.actionsOpen) actionsLoader.item.close()
            else if (win.running) backend.stop()
            else win.hide()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Rectangle {
        id: panel
        anchors.fill: parent
        radius: theme.radius
        color: Palette.surface
        border.color: Palette.border
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Palette.unit * 2
                Layout.rightMargin: Palette.unit * 2
                Layout.topMargin: Palette.unit * 1.5
                Layout.bottomMargin: Palette.unit * 1.5
                spacing: Palette.unit * 1.5

                Text {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    text: "✦"
                    color: theme.accent
                    font.pixelSize: 20
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(Math.max(input.implicitHeight, 28), 112)
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: input
                        focus: true
                        padding: 0
                        topPadding: 2
                        bottomPadding: 2
                        background: null
                        wrapMode: TextArea.Wrap
                        color: Palette.text
                        font.pixelSize: 18
                        placeholderText: qsTr("Ask AI anything…")
                        placeholderTextColor: Palette.faint
                        selectionColor: theme.accent
                        selectedTextColor: theme.accentForeground
                        cursorDelegate: Rectangle { width: 2; color: theme.accent }

                        Keys.onPressed: event => {
                            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            if (enter && !(event.modifiers & Qt.ShiftModifier)) {
                                event.accepted = true
                                if (text.length === 0 && !win.hasMessages && win.exampleIndex >= 0)
                                    win.submit(win.examples[win.exampleIndex])
                                else
                                    win.submit(text)
                            } else if (text.length === 0 && event.key === Qt.Key_Up) {
                                event.accepted = true
                                if (!win.hasMessages)
                                    win.exampleIndex = win.exampleIndex <= 0 ? win.examples.length - 1 : win.exampleIndex - 1
                                else {
                                    text = backend.lastQuestion()
                                    cursorPosition = text.length
                                }
                            } else if (text.length === 0 && event.key === Qt.Key_Down && !win.hasMessages) {
                                event.accepted = true
                                win.exampleIndex = (win.exampleIndex + 1) % win.examples.length
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.divider }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                EmptyState {
                    anchors.fill: parent
                    visible: !win.hasMessages
                    examples: win.examples
                    selected: win.exampleIndex
                    onSelectedChanged: win.exampleIndex = selected
                    onPicked: index => win.submit(win.examples[index])
                }

                ListView {
                    id: list
                    anchors.fill: parent
                    visible: win.hasMessages
                    clip: true
                    model: backend.messages
                    spacing: Palette.unit * 1.5
                    topMargin: Palette.unit * 2
                    bottomMargin: Palette.unit * 2
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: false
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        implicitWidth: 6
                        contentItem: Rectangle { implicitWidth: 4; radius: 2; color: Palette.faint; opacity: parent.active ? 0.8 : 0.4 }
                    }

                    // Stays pinned to the newest text until the user scrolls away.
                    property bool follow: true
                    readonly property real endY: Math.max(0, contentHeight + bottomMargin - height)
                    onContentHeightChanged: if (follow) {
                        glide.stop()
                        glide.to = endY
                        glide.restart()
                    }
                    onMovementStarted: { follow = false; glide.stop() }
                    onMovementEnded: follow = atYEnd
                    NumberAnimation { id: glide; target: list; property: "contentY"; duration: 160; easing.type: Easing.OutCubic }

                    delegate: Item {
                        id: row
                        required property int index
                        width: list.width
                        height: message.implicitHeight
                        Message {
                            id: message
                            x: Palette.unit * 2.5
                            width: parent.width - Palette.unit * 5
                            kind: model.role
                            raw: model.text
                            markdown: model.display
                            phase: model.state
                            error: backend.errorText
                            isLast: row.index === list.count - 1
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.divider }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.leftMargin: Palette.unit * 2
                Layout.rightMargin: Palette.unit * 2
                spacing: Palette.unit

                Text {
                    text: "✦"
                    color: Palette.faint
                    font.pixelSize: 13
                }
                Text {
                    text: qsTr("Ask AI")
                    color: Palette.dim
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                StatusDot { visible: win.running; Layout.alignment: Qt.AlignVCenter }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: backend.status === Backend.Failed ? Palette.danger : Palette.faint
                    text: {
                        if (backend.status === Backend.Running)
                            return backend.messages.lastHasText ? qsTr("Streaming…") : qsTr("Thinking…")
                        if (backend.status === Backend.Failed)
                            return backend.errorText.split("\n")[0]
                        if (backend.status === Backend.Stopped)
                            return qsTr("Stopped")
                        return ""
                    }
                }

                Text { visible: win.running; text: qsTr("Stop"); color: Palette.dim; font.pixelSize: 12 }
                KeyCap { visible: win.running; text: "Esc" }
                Text { visible: !win.running; text: qsTr("Send"); color: Palette.dim; font.pixelSize: 12 }
                KeyCap { visible: !win.running; text: "↵" }
                Rectangle { width: 1; height: 16; color: Palette.divider }
                Text { text: qsTr("Actions"); color: Palette.dim; font.pixelSize: 12 }
                KeyCap { text: "Ctrl" }
                KeyCap { text: "K" }
            }
        }
    }

    Loader {
        id: actionsLoader
        anchors.fill: parent
        active: false
        source: "ActionsPanel.qml"
    }
    Binding { target: actionsLoader.item; property: "actions"; value: win.actionItems; when: actionsLoader.item !== null }
    Connections {
        target: actionsLoader.item
        function onTriggered(id) { win.runAction(id) }
        function onDismissed() { input.forceActiveFocus() }
    }

    Loader {
        anchors.fill: parent
        active: win.helpVisible
        sourceComponent: ShortcutsOverlay {
            rows: [
                {label: qsTr("Send"), keys: ["↵"]},
                {label: qsTr("New line"), keys: ["Shift", "↵"]},
                {label: qsTr("Stop, then hide"), keys: ["Esc"]},
                {label: qsTr("Actions"), keys: ["Ctrl", "K"]},
                {label: qsTr("New Chat"), keys: ["Ctrl", "N"]},
                {label: qsTr("Copy Answer"), keys: ["Ctrl", "Shift", "C"]},
                {label: qsTr("Recall last question"), keys: ["↑"]},
                {label: qsTr("Quit"), keys: ["Ctrl", "Q"]},
                {label: qsTr("Keyboard Shortcuts"), keys: ["F1"]}
            ]
            onDismissed: win.helpVisible = false
        }
    }
}
