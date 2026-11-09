// ask: a Raycast-style popup that asks `fx ask` a question and streams the answer.

#include <QDateTime>
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
#include <utility>

#include "backend.h"
#include "context.h"
#include "instance.h"
#include "settings.h"
#include "theme.h"
#include "translator.h"

namespace {
QElapsedTimer startup;
bool tracing = false;

void trace(const char *what, qint64 since) {
    if (tracing)
        std::fprintf(stderr, "ask-trace: %s %lld ms\n", what, static_cast<long long>(since));
}

// Calls `then` once, after the next frame reaches the screen. frameSwapped is
// emitted on the render thread, so several queued calls can arrive before the
// disconnect lands; the flag keeps only the first.
void afterNextFrame(QQuickWindow *window, std::function<void()> then) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto fired = std::make_shared<bool>(false);
    *connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [connection, fired, then] {
        if (std::exchange(*fired, true))
            return;
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
    // costs a socket round trip and no platform plugin. `--background` only
    // pings: autostarting twice must not toggle the window.
    Instance instance;
    std::optional<Context> initial;
    if (background) {
        if (instance.pingRunning())
            return 0;
    } else {
        const auto outcome = instance.toggleRunning([] {
            QElapsedTimer gathering;
            gathering.start();
            const Context context = Context::gather();
            trace("context gathered", gathering.elapsed());
            return context;
        });
        if (outcome != Instance::NotRunning)
            return 0;
        // First launch: we are the instance, and the window has not stolen focus yet.
        initial = Context::gather();
    }

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
    Backend backend({}, {}, Backend::Paths().withSettings(Settings::defaultPath()));
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

    instance.setVisibleProbe([window] { return window->isVisible(); });
    QObject::connect(&instance, &Instance::hideRequested, window, [window] { window->hide(); });
    QObject::connect(&instance, &Instance::showRequested, window, [window, present, &backend](const std::optional<Context> &context) {
        QElapsedTimer since;
        since.start();
        backend.applyContext(context);
        if (tracing)
            afterNextFrame(window, [since] {
                trace("show to frame", since.elapsed());
                std::fprintf(stderr, "ask-trace: frame at %lld ms epoch\n",
                             static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
            });
        present();
    });

    // Created but not shown, so the first toggle pays for as little as possible.
    if (background)
        window->create();
    else {
        backend.applyContext(initial);
        present();
    }

    return app.exec();
}
