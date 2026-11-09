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
    property string panelMode: "actions"
    readonly property bool commandMode: backend.attachments.hasEnabledSelection
    readonly property var commandList: backend.commands()
    readonly property bool actionsOpen: actionsLoader.item !== null && actionsLoader.item.visible

    readonly property var examples: [
        qsTr("Explain how TCP slow start works"),
        qsTr("Write a regex that matches ISO 8601 dates"),
        qsTr("What is the difference between a mutex and a semaphore?")
    ]

    // What the empty state offers: AI commands for a selection, else example prompts.
    readonly property var emptyItems: commandMode ? commandList.map(c => qsTr(c.title)) : examples
    readonly property string emptyHint: commandMode ? qsTr("Choose what to do with the selected text.")
                                                    : qsTr("Ask a question and the answer streams in here.")

    readonly property var actionItems: {
        const items = []
        if (running)
            items.push({id: "stop", label: qsTr("Stop Generating"), keys: ["Esc"]})
        if (backend.messages.lastHasText)
            items.push({id: "copyAnswer", label: qsTr("Copy Answer"), keys: ["Ctrl", "Shift", "C"]})
        if (hasMessages)
            items.push({id: "copyQuestion", label: qsTr("Copy Question"), keys: []})
        if (commandMode)
            for (const c of commandList)
                items.push({id: "command:" + c.id, label: qsTr(c.title), keys: []})
        if (backend.attachments.hasScreen)
            items.push({id: "screen", keys: ["Ctrl", "S"],
                        label: backend.attachments.screenEnabled ? qsTr("Remove Window Screenshot") : qsTr("Attach Window Screenshot")})
        items.push({id: "changeModel", label: qsTr("Change Model…"), keys: []})
        items.push({id: "newChat", label: qsTr("New Chat"), keys: ["Ctrl", "N"]})
        items.push({id: "shortcuts", label: qsTr("Keyboard Shortcuts"), keys: ["F1"]})
        return items
    }

    readonly property var modelItems: {
        const items = [{id: "model:", label: qsTr("Default (fx)"), keys: [], checked: backend.model === ""}]
        if (backend.modelsState === Backend.ModelsLoading)
            items.push({id: "wait", label: qsTr("Loading models…"), keys: []})
        else if (backend.modelsState === Backend.ModelsFailed)
            items.push({id: "retry", label: qsTr("Could not load models. Enter to retry."), keys: []})
        for (const m of backend.models)
            items.push({id: "model:" + m, label: m, keys: [], checked: m === backend.model})
        return items
    }

    function focusInput() {
        input.forceActiveFocus()
    }

    function runAction(id) {
        if (id === "wait")
            return
        if (actionsLoader.item)
            actionsLoader.item.close()
        if (id === "stop") backend.stop()
        else if (id === "copyAnswer") backend.copyLastAnswer()
        else if (id === "copyQuestion") backend.copyLastQuestion()
        else if (id === "newChat") newChat()
        else if (id === "shortcuts") helpVisible = true
        else if (id === "screen") backend.toggleScreen()
        else if (id === "changeModel") openModels()
        else if (id === "retry") { backend.loadModels(); openModels() }
        else if (id.startsWith("model:")) backend.setModel(id.slice(6))
        else if (id.startsWith("command:")) runCommand(id.slice(8))
    }

    function runCommand(id) {
        list.follow = true
        if (backend.runCommand(id))
            exampleIndex = -1
    }

    function pickEmpty(index) {
        if (commandMode) runCommand(commandList[index].id)
        else submit(examples[index])
    }

    function openActions() {
        panelMode = "actions"
        actionsLoader.active = true
        actionsLoader.item.open()
    }

    function openModels() {
        panelMode = "models"
        backend.loadModels()
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
        // Refused while a chip is still capturing; the text stays for another Enter.
        if (backend.send(text)) {
            input.clear()
            exampleIndex = -1
        }
    }

    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: win.actionsOpen ? actionsLoader.item.close() : win.openActions() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.toggleScreen() }
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
                        objectName: "input"
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
                                    win.pickEmpty(win.exampleIndex)
                                else
                                    win.submit(text)
                            } else if (event.matches(StandardKey.Paste) && backend.pasteImage()) {
                                event.accepted = true
                            } else if (text.length === 0 && event.key === Qt.Key_Backspace && backend.removeLastAttachment()) {
                                event.accepted = true
                            } else if (text.length === 0 && event.key === Qt.Key_Up) {
                                event.accepted = true
                                if (!win.hasMessages)
                                    win.exampleIndex = win.exampleIndex <= 0 ? win.emptyItems.length - 1 : win.exampleIndex - 1
                                else {
                                    text = backend.lastQuestion()
                                    cursorPosition = text.length
                                }
                            } else if (text.length === 0 && event.key === Qt.Key_Down && !win.hasMessages) {
                                event.accepted = true
                                win.exampleIndex = (win.exampleIndex + 1) % win.emptyItems.length
                            }
                        }
                    }
                }
            }

            AttachmentChips {
                Layout.fillWidth: true
                Layout.leftMargin: Palette.unit * 2 + 20 + Palette.unit * 1.5
                Layout.rightMargin: Palette.unit * 2
                Layout.bottomMargin: Palette.unit * 1.25
                // Above the transcript so a chip's tooltip is not covered.
                z: 5
                visible: backend.attachments.count > 0
                onToggled: row => backend.toggleAttachment(row)
                onRemoved: row => { backend.removeAttachment(row); input.forceActiveFocus() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Palette.divider }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                EmptyState {
                    anchors.fill: parent
                    visible: !win.hasMessages
                    items: win.emptyItems
                    hint: win.emptyHint
                    selected: win.exampleIndex
                    onSelectedChanged: win.exampleIndex = selected
                    onPicked: index => win.pickEmpty(index)
                }

                ListView {
                    id: list
                    objectName: "list"
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
                    readonly property real endY: Math.max(originY - topMargin, originY + contentHeight + bottomMargin - height)
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
                            note: model.note
                            error: backend.errorText
                            isLast: index === list.count - 1
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
                Rectangle { width: 1; height: 16; color: Palette.divider }
                Text {
                    id: modelLabel
                    Layout.maximumWidth: 180
                    text: backend.modelLabel.length ? backend.modelLabel : qsTr("Default model")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: modelHover.hovered ? Palette.text : Palette.faint
                    font.pixelSize: 12
                    HoverHandler { id: modelHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: win.openModels() }
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

    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: event => { if (event.hasUrls) backend.addImageFiles(event.urls) }
    }
    Rectangle {
        anchors.fill: parent
        visible: drop.containsDrag
        radius: theme.radius
        color: Qt.alpha(theme.accent, 0.08)
        border.color: theme.accent
        Text {
            anchors.centerIn: parent
            text: qsTr("Drop images to attach")
            color: theme.accent
            font.pixelSize: 15
        }
    }

    Loader {
        id: actionsLoader
        anchors.fill: parent
        active: false
        source: "ActionsPanel.qml"
    }
    Binding { target: actionsLoader.item; property: "actions"; value: win.panelMode === "models" ? win.modelItems : win.actionItems; when: actionsLoader.item !== null }
    Binding { target: actionsLoader.item; property: "placeholder"; value: win.panelMode === "models" ? qsTr("Search for models…") : qsTr("Search for actions…"); when: actionsLoader.item !== null }
    Binding { target: actionsLoader.item; property: "popoverWidth"; value: win.panelMode === "models" ? 380 : 280; when: actionsLoader.item !== null }
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
                {label: qsTr("Attach window screenshot"), keys: ["Ctrl", "S"]},
                {label: qsTr("Paste image"), keys: ["Ctrl", "V"]},
                {label: qsTr("Remove last attachment"), keys: ["⌫"]},
                {label: qsTr("Recall last question"), keys: ["↑"]},
                {label: qsTr("Quit"), keys: ["Ctrl", "Q"]},
                {label: qsTr("Keyboard Shortcuts"), keys: ["F1"]}
            ]
            onDismissed: win.helpVisible = false
        }
    }
}
