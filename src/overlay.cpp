#include "overlay.h"

#include <QGuiApplication>
#include <QQuickWindow>

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>

namespace {
bool shouldUseLayerShell() {
    return QGuiApplication::platformName().startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive) &&
           qEnvironmentVariableIntValue("ASK_NO_LAYER_SHELL") == 0;
}
}
#endif

namespace Overlay {

void configureWindow(QQuickWindow *window) {
#ifdef HAVE_LAYER_SHELL
    if (!window || !shouldUseLayerShell())
        return;
    if (auto *ls = LayerShellQt::Window::get(window)) {
        ls->setScope(QStringLiteral("ask"));
        ls->setLayer(LayerShellQt::Window::LayerOverlay);
        ls->setAnchors(LayerShellQt::Window::AnchorNone);
        ls->setExclusiveZone(-1);
        ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        ls->setActivateOnShow(true);
        ls->setWantsToBeOnActiveScreen(true);
        ls->setCloseOnDismissed(false);
        ls->setDesiredSize(window->size());

        auto syncSize = [window, ls] { ls->setDesiredSize(window->size()); };
        QObject::connect(window, &QWindow::widthChanged, ls, syncSize);
        QObject::connect(window, &QWindow::heightChanged, ls, syncSize);
    }
#else
    (void)window;
#endif
}

void prepareForShow(QQuickWindow *window) {
#ifdef HAVE_LAYER_SHELL
    if (!window || !shouldUseLayerShell())
        return;
    if (auto *ls = LayerShellQt::Window::get(window)) {
        ls->setWantsToBeOnActiveScreen(true);
        ls->setDesiredSize(window->size());
    }
#else
    (void)window;
#endif
}

}
