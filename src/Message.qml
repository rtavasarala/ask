import QtQuick
import Ask

// One turn: a compact dim line for the user, clean Markdown for the answer.
Item {
    id: root
    property string kind
    property string raw
    property string markdown
    required property var blocks
    property string phase
    property string error
    property bool isLast: false
    property string note

    readonly property bool streaming: phase === "streaming"
    // Indexed by count so existing delegates stay alive while chunks stream in.
    readonly property var effectiveBlocks: {
        if (blocks && blocks.length > 0)
            return blocks
        if (markdown.length > 0)
            return [{isCode: false, language: "", body: markdown.replace(/\n$/, "")}]
        return []
    }

    implicitHeight: kind === "user" ? userLine.implicitHeight + 8 : answer.implicitHeight + 8

    Row {
        id: userLine
        visible: root.kind === "user"
        width: parent.width
        spacing: Palette.unit
        Text {
            id: tag
            text: qsTr("You")
            color: Palette.faint
            font.pixelSize: 11
            font.weight: Font.DemiBold
            topPadding: 2
            width: 28
        }
        Column {
            width: parent.width - tag.width - parent.spacing
            spacing: 2
            Text {
                width: parent.width
                text: root.raw
                textFormat: Text.PlainText
                color: Palette.dim
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Text {
                visible: root.note.length > 0
                width: parent.width
                text: "＋ " + root.note
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Palette.faint
                font.pixelSize: 11
            }
        }
    }

    Column {
        id: answer
        visible: root.kind === "assistant"
        width: parent.width
        spacing: Palette.unit

        Repeater {
            model: root.effectiveBlocks.length
            delegate: Item {
                id: part
                required property int index
                readonly property var block: root.effectiveBlocks[index] || ({isCode: false, language: "", body: ""})
                readonly property bool isCode: Boolean(block.isCode)
                readonly property string language: block.language || ""
                readonly property string body: block.body || ""
                readonly property bool last: index === root.effectiveBlocks.length - 1
                visible: isCode || body.length > 0
                width: answer.width
                height: visible ? (isCode ? codeBox.height : prose.implicitHeight) : -answer.spacing

                TextEdit {
                    id: prose
                    visible: !part.isCode
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.MarkdownText
                    text: part.body
                    wrapMode: TextEdit.Wrap
                    color: Palette.text
                    font.pixelSize: 14
                    selectionColor: theme.accent
                    selectedTextColor: theme.accentForeground
                    onLinkActivated: link => { if (/^https?:/i.test(link)) Qt.openUrlExternally(link) }
                }

                Rectangle {
                    id: codeBox
                    visible: part.isCode
                    width: parent.width
                    height: code.y + code.implicitHeight + Palette.unit
                    radius: Math.min(6, theme.radius)
                    color: Palette.codeBackground
                    border.color: Palette.divider

                    Item {
                        id: codeHeader
                        width: parent.width
                        height: 26

                        Text {
                            x: Palette.unit + 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, copyAction.x - x - Palette.unit)
                            text: part.language.length > 0 ? part.language : qsTr("code")
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Palette.faint
                            font.family: Palette.mono
                            font.pixelSize: 11
                        }

                        Item {
                            id: copyAction
                            property bool copied: false
                            anchors.right: parent.right
                            anchors.rightMargin: Palette.unit - 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: copyLabel.implicitWidth + Palette.unit * 1.5
                            height: 20

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.min(4, theme.radius)
                                color: copyHover.hovered ? Palette.hover : "transparent"
                            }
                            Text {
                                id: copyLabel
                                anchors.centerIn: parent
                                text: copyAction.copied ? ("✓ " + qsTr("Copied")) : qsTr("Copy")
                                textFormat: Text.PlainText
                                color: copyAction.copied ? theme.accent : (copyHover.hovered ? Palette.text : Palette.dim)
                                font.pixelSize: 11
                            }
                            HoverHandler { id: copyHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                enabled: part.body.length > 0
                                onTapped: {
                                    backend.copyText(part.body)
                                    copyAction.copied = true
                                    copiedTimer.restart()
                                }
                            }
                            Timer {
                                id: copiedTimer
                                interval: 1200
                                onTriggered: copyAction.copied = false
                            }
                        }
                    }

                    Rectangle {
                        y: codeHeader.height
                        width: parent.width
                        height: 1
                        color: Palette.divider
                    }

                    TextEdit {
                        id: code
                        x: Palette.unit + 2
                        y: codeHeader.height + 1 + Palette.unit
                        width: parent.width - Palette.unit * 2 - 4
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        text: part.body
                        wrapMode: TextEdit.Wrap
                        color: Palette.text
                        font.family: Palette.mono
                        font.pixelSize: 13
                        selectionColor: theme.accent
                        selectedTextColor: theme.accentForeground
                    }
                }

                // Follows the end of the text while the answer streams in.
                Rectangle {
                    readonly property var end: {
                        const edit = part.isCode ? code : prose
                        edit.length; edit.width; edit.contentHeight
                        return edit.positionToRectangle(edit.length)
                    }
                    visible: root.streaming && part.last
                    x: (part.isCode ? codeBox.x + code.x : prose.x) + end.x + 1
                    y: (part.isCode ? codeBox.y + code.y : prose.y) + end.y + 2
                    width: 2
                    height: Math.max(12, end.height - 4)
                    color: theme.accent
                    SequentialAnimation on opacity {
                        running: parent.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.15; duration: 500; easing.type: Easing.OutQuad }
                        NumberAnimation { from: 0.15; to: 1; duration: 500; easing.type: Easing.OutQuad }
                    }
                }
            }
        }

        StatusDot {
            visible: root.streaming && root.raw.length === 0
        }

        Column {
            visible: root.phase === "failed" && root.isLast
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: root.error
                textFormat: Text.PlainText
                color: Palette.danger
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Rectangle {
                width: retryLabel.implicitWidth + Palette.unit * 2
                height: 24
                radius: Math.min(6, theme.radius)
                color: retryHover.hovered ? Palette.hover : "transparent"
                border.color: retryHover.hovered ? Palette.dim : Palette.border

                Text {
                    id: retryLabel
                    anchors.centerIn: parent
                    text: qsTr("Retry")
                    textFormat: Text.PlainText
                    color: retryHover.hovered ? Palette.text : Palette.dim
                    font.pixelSize: 12
                }
                HoverHandler { id: retryHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: backend.regenerate() }
            }
        }
    }
}
