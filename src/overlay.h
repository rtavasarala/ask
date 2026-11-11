#pragma once

class QQuickWindow;

// Places the popup on Wayland's wlr-layer-shell overlay layer so it opens
// centered on the active monitor and above fullscreen or special workspaces.
// Non-Wayland platforms (offscreen tests, X11) and ASK_NO_LAYER_SHELL=1 no-op.
namespace Overlay {
void configureWindow(QQuickWindow *window);
void prepareForShow(QQuickWindow *window);
}
