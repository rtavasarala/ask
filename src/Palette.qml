pragma Singleton
import QtQuick

// Neutral surface tokens. The accent and corner radius come from `theme`.
QtObject {
    readonly property color surface: "#161618"
    readonly property color border: "#26ffffff"
    readonly property color divider: "#14ffffff"
    readonly property color text: "#ececee"
    readonly property color dim: "#9a9aa2"
    readonly property color faint: "#64646b"
    readonly property color hover: "#0fffffff"
    readonly property color selected: "#1affffff"
    readonly property color keycap: "#1affffff"
    readonly property color codeBackground: "#0dffffff"
    readonly property color danger: "#ff6b6b"
    readonly property string mono: "monospace"
    readonly property int unit: 8
    readonly property int quick: 140
}
