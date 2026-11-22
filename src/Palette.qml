pragma Singleton
import QtQuick

// Surface and typography tokens; `Main.qml` binds `source` to `theme`, with dark fallbacks.
QtObject {
    property var source: null

    readonly property bool light: source ? source.light : false
    readonly property color surface: source ? source.surface : "#161618"
    readonly property color popover: source ? source.popover : "#202023"
    readonly property color border: source ? source.border : "#26ffffff"
    readonly property color divider: source ? source.divider : "#14ffffff"
    readonly property color text: source ? source.text : "#ececee"
    readonly property color dim: source ? source.dim : "#9a9aa2"
    readonly property color faint: source ? source.faint : "#64646b"
    readonly property color hover: source ? source.hover : "#0fffffff"
    readonly property color selected: source ? source.selected : "#1affffff"
    readonly property color keycap: source ? source.keycap : "#1affffff"
    readonly property color codeBackground: source ? source.codeBackground : "#0dffffff"
    readonly property color danger: source ? source.danger : "#ff6b6b"
    readonly property color shadow: source ? source.shadow : "#66000000"
    readonly property color scrim: source ? source.scrim : "#99000000"
    readonly property string mono: source ? source.monoFont : "monospace"
    readonly property int unit: 8
    readonly property int quick: 140
}
