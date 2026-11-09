// ask: a Raycast-style popup that asks `fx ask` a question and streams the answer.

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#include "backend.h"
#include "instance.h"
#include "theme.h"
#include "translator.h"

namespace {
QElapsedTimer startup;
bool tracing = false;

void trace(const char *what, qint64 since) {
    if (tracing)
        std::fprintf(stderr, "ask-trace: %s %lld ms\n", what, static_cast<long long>(since));
}

// Calls `then` once, after the next frame reaches the screen.
void afterNextFrame(QQuickWindow *window, std::function<void()> then) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [connection, then] {
        QObject::disconnect(*connection);
        then();
    });
}
}

int main(int argc, char *argv[]) {
    startup.start();
    tracing = qEnvironmentVariableIntValue("ASK_TRACE") != 0;

    bool background = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--background"))
            background = true;

    // Before the GUI exists, so a hotkey press that lands on a warm process
    // costs one socket write and no platform plugin. `--background` only pings:
    // autostarting twice must not toggle the window.
    Instance instance;
    if (instance.notifyRunning(background ? "ping\n" : "toggle\n"))
        return 0;

    QGuiApplication app(argc, argv);
    app.setApplicationName("ask");
    app.setApplicationVersion("0.1.0");
    // Hiding the window keeps the conversation and the warm process.
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName("ask");
    app.setWindowIcon(QIcon::fromTheme("ask"));

    QQuickStyle::setStyle("Basic");

    Translator translator;
    app.installTranslator(&translator);

    instance.listen();

    Theme theme;
    Backend backend;
    qmlRegisterSingletonType(QUrl("qrc:/Palette.qml"), "Ask", 1, 0, "Palette");
    qmlRegisterUncreatableType<Backend>("Ask", 1, 0, "Backend", "Backend is provided by the application");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    trace("qml loaded", startup.elapsed());

    auto present = [window] {
        window->show();
        window->raise();
        window->requestActivate();
        QMetaObject::invokeMethod(window, "focusInput");
    };

    if (tracing)
        afterNextFrame(window, [] { trace("first frame", startup.elapsed()); });

    QObject::connect(&instance, &Instance::toggleRequested, window, [window, present] {
        if (window->isVisible() && window->isActive()) {
            window->hide();
            return;
        }
        if (tracing) {
            QElapsedTimer since;
            since.start();
            afterNextFrame(window, [since] { trace("toggle to frame", since.elapsed()); });
        }
        present();
    });

    // Created but not shown, so the first toggle pays for as little as possible.
    if (background)
        window->create();
    else
        present();

    return app.exec();
}
