import QtQuick
import Ask

// One turn: a compact dim line for the user, clean Markdown for the answer.
Item {
    id: root
    property string kind
    property string raw
    property string markdown
    property string phase
    property string error
    property bool isLast: false

    readonly property bool streaming: phase === "streaming"
    // Fenced code is split out so it can sit on its own background. Even
    // parts are prose, odd parts are code, so a segment keeps its kind while
    // text streams in and delegates are not rebuilt per chunk.
    readonly property var parts: markdown.split(/^```[^\n]*\n?/m)

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
        Text {
            width: parent.width - tag.width - parent.spacing
            text: root.raw
            color: Palette.dim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    Column {
        id: answer
        visible: root.kind === "assistant"
        width: parent.width
        spacing: Palette.unit

        Repeater {
            model: root.parts.length
            delegate: Item {
                id: part
                required property int index
                readonly property bool isCode: index % 2 === 1
                readonly property string body: root.parts[index].replace(/\n$/, "")
                readonly property bool last: index === root.parts.length - 1
                visible: body.length > 0 || (isCode && !last)
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
                    height: code.implicitHeight + Palette.unit * 2
                    radius: Math.min(6, theme.radius)
                    color: Palette.codeBackground
                    border.color: Palette.divider

                    TextEdit {
                        id: code
                        x: Palette.unit + 2
                        y: Palette.unit
                        width: parent.width - Palette.unit * 2 - 4
                        readOnly: true
                        selectByMouse: true
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
                    x: (part.isCode ? code.x : 0) + end.x + 1
                    y: (part.isCode ? code.y : 0) + end.y + 2
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

        Text {
            visible: root.phase === "failed" && root.isLast
            width: parent.width
            text: root.error
            color: Palette.danger
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }
}
