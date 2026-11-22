import QtQuick
import Ask

// One turn: a compact dim line for the user, clean Markdown for the answer.
Item {
    id: root
    property string kind
    property string raw
    property var blocksModel: null
    property bool hasText: false
    property string phase
    property string error
    property bool isLast: false
    property string note

    readonly property bool streaming: phase === "streaming"

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

    Component {
        id: proseComponent
        TextEdit {
            id: prose
            readonly property var targetEdit: prose
            readonly property real editOffsetX: 0
            readonly property real editOffsetY: 0
            width: parent ? parent.width : 0
            height: implicitHeight
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.MarkdownText
            text: parent ? parent.blockBody : ""
            wrapMode: TextEdit.Wrap
            color: Palette.text
            font.pixelSize: 14
            selectionColor: theme.accent
            selectedTextColor: theme.accentForeground
            onLinkActivated: link => { if (/^https?:/i.test(link)) Qt.openUrlExternally(link) }
        }
    }

    Component {
        id: codeComponent
        Rectangle {
            id: codeBox
            readonly property var targetEdit: code
            readonly property real editOffsetX: code.x
            readonly property real editOffsetY: code.y
            readonly property string blockLanguage: parent ? parent.blockLanguage : ""
            readonly property string blockBody: parent ? parent.blockBody : ""
            width: parent ? parent.width : 0
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
                    text: codeBox.blockLanguage.length > 0 ? codeBox.blockLanguage : qsTr("code")
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
                        enabled: codeBox.blockBody.length > 0
                        onTapped: {
                            backend.copyText(codeBox.blockBody)
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
                text: codeBox.blockBody
                wrapMode: TextEdit.Wrap
                color: Palette.text
                font.family: Palette.mono
                font.pixelSize: 13
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
            }
        }
    }

    Component {
        id: cursorComponent
        Rectangle {
            readonly property var edit: parent ? parent.targetEdit : null
            readonly property var end: {
                if (!edit)
                    return {x: 0, y: 0, height: 16}
                edit.length; edit.width; edit.contentHeight
                return edit.positionToRectangle(edit.length)
            }
            x: (parent ? parent.offsetX : 0) + end.x + 1
            y: (parent ? parent.offsetY : 0) + end.y + 2
            width: 2
            height: Math.max(12, end.height - 4)
            color: theme.accent
            SequentialAnimation on opacity {
                running: true
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.15; duration: 500; easing.type: Easing.OutQuad }
                NumberAnimation { from: 0.15; to: 1; duration: 500; easing.type: Easing.OutQuad }
            }
        }
    }

    Column {
        id: answer
        visible: root.kind === "assistant"
        width: parent.width
        spacing: Palette.unit

        Repeater {
            id: blockRepeater
            model: root.kind === "assistant" ? root.blocksModel : null
            delegate: Item {
                id: part
                required property int index
                required property bool isCode
                required property string language
                required property string body
                readonly property bool last: index === blockRepeater.count - 1
                visible: isCode || body.length > 0
                width: answer.width
                height: visible ? (blockLoader.item ? blockLoader.item.height : 0) : -answer.spacing

                Loader {
                    id: blockLoader
                    width: parent.width
                    readonly property string blockLanguage: part.language
                    readonly property string blockBody: part.body
                    sourceComponent: part.isCode ? codeComponent : proseComponent
                }

                // Follows the end of the text while the answer streams in.
                Loader {
                    active: root.streaming && part.last && blockLoader.item !== null
                    readonly property var targetEdit: blockLoader.item ? blockLoader.item.targetEdit : null
                    readonly property real offsetX: blockLoader.item ? blockLoader.item.editOffsetX : 0
                    readonly property real offsetY: blockLoader.item ? blockLoader.item.editOffsetY : 0
                    sourceComponent: cursorComponent
                }
            }
        }

        StatusDot {
            visible: root.streaming && !root.hasText
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
