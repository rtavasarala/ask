// ask: a Raycast-style popup that asks `fx ask` a question and streams the answer.

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

#include "backend.h"
#include "cli.h"
#include "commands.h"
#include "context.h"
#include "instance.h"
#include "overlay.h"
#include "settings.h"
#include "theme.h"
#include "translator.h"

namespace {
QElapsedTimer startup;
bool tracing = false;

// Wall-clock time of an event, so a shell can subtract its own start time.
void traceEpoch(const char *what) {
    if (tracing)
        std::fprintf(stderr, "ask-trace: %s at %lld ms epoch\n", what, static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
}

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

    const ParsedCli cli = parseCli(argc, argv);
    if (cli.mode == ParsedCli::PrintHelp) {
        const QByteArray help = cliHelpText().toUtf8();
        std::fwrite(help.constData(), 1, size_t(help.size()), stdout);
        return 0;
    }
    if (cli.mode == ParsedCli::PrintVersion) {
        std::puts("ask 0.3.0");
        return 0;
    }
    if (cli.mode == ParsedCli::Error) {
        const QByteArray err = (QStringLiteral("ask: ") + cli.error + QLatin1Char('\n')).toUtf8();
        std::fwrite(err.constData(), 1, size_t(err.size()), stderr);
        return 2;
    }
    if (cli.mode == ParsedCli::QueryHeadless)
        return runHeadlessQuery(cli, argc, argv);

    auto gatherWithTrace = [] {
        QElapsedTimer gathering;
        gathering.start();
        const Context context = Context::gather();
        trace("context gathered", gathering.elapsed());
        return context;
    };

    Instance::Request initialRequest;
    if (cli.mode == ParsedCli::OpenGui) {
        initialRequest.prompt = cli.prompt;
        initialRequest.commandId = cli.commandId;
        initialRequest.newChat = cli.newChat;
        for (const QString &file : cli.files)
            initialRequest.files << QFileInfo(file).absoluteFilePath();
        initialRequest.model = cli.model;
        initialRequest.effort = cli.effort;
        initialRequest.fast = cli.fast;
    }

    // Before the GUI exists, so a hotkey press that lands on a warm process
    // costs one socket round trip and no platform plugin. `--background` only
    // pings: autostarting twice must not toggle the window.
    Instance instance;
    std::optional<Context> initial;
    if (cli.mode == ParsedCli::BackgroundGui) {
        if (instance.pingRunning())
            return 0;
    } else if (cli.mode == ParsedCli::OpenGui) {
        if (instance.sendRequest(initialRequest, gatherWithTrace) != Instance::NotRunning)
            return 0;
        initial = gatherWithTrace();
    } else {
        if (instance.toggleRunning(gatherWithTrace) != Instance::NotRunning)
            return 0;
        // First launch: we are the instance, and the window has not stolen focus yet.
        initial = gatherWithTrace();
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("ask");
    app.setApplicationVersion("0.3.0");
    // Hiding the window keeps the conversation and the warm process.
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName("ask");
    app.setWindowIcon(QIcon::fromTheme("ask"));

    QQuickStyle::setStyle("Basic");

    Translator translator;
    app.installTranslator(&translator);

    instance.setContextGatherer(gatherWithTrace);
    instance.listen();

    Theme theme;
    Backend backend({}, {}, Backend::Paths().withSettings(Settings::defaultPath()).withCommands(defaultCommandsPath()));
    backend.setUiLanguage(translator.language());
    QObject::connect(&translator, &Translator::changed, &backend, [&translator, &backend] {
        backend.setUiLanguage(translator.language());
    });

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

    Overlay::configureWindow(window);

    auto present = [window] {
        Overlay::prepareForShow(window);
        window->show();
        window->raise();
        window->requestActivate();
        QMetaObject::invokeMethod(window, "focusInput");
    };

    auto applyRequest = [&backend, window](const std::optional<Context> &context, const Instance::Request &req) {
        if (req.newChat)
            backend.newChat();
        if (context.has_value())
            backend.applyContext(context);
        if (!req.model.isEmpty())
            backend.setModel(req.model);
        if (!req.effort.isEmpty())
            backend.setEffort(req.effort);
        if (req.fast)
            backend.setFast(true);
        if (!req.files.isEmpty()) {
            QList<QUrl> urls;
            for (const QString &path : req.files)
                urls << QUrl::fromLocalFile(path);
            backend.addFiles(urls);
        }
        if (!req.commandId.isEmpty()) {
            if (!backend.runCommand(req.commandId) && !req.prompt.isEmpty())
                backend.send(req.prompt);
        } else if (!req.prompt.isEmpty()) {
            if (!backend.send(req.prompt)) {
                if (auto *input = window->findChild<QObject *>("input"))
                    input->setProperty("text", req.prompt);
            }
        }
    };

    if (tracing)
        afterNextFrame(window, [] { trace("first frame", startup.elapsed()); });

    QObject::connect(&backend, &Backend::hideWindowRequested, window, [window] { window->hide(); });
    QObject::connect(&backend, &Backend::showWindowRequested, window, [present] { present(); });

    instance.setVisibleProbe([window] { return window->isVisible(); });
    QObject::connect(&instance, &Instance::hideRequested, window, [window] {
        window->hide();
        traceEpoch("hidden");
    });
    QObject::connect(&instance, &Instance::showRequested, window, [window, present, &backend](const std::optional<Context> &context) {
        QElapsedTimer since;
        since.start();
        backend.applyContext(context);
        if (tracing)
            afterNextFrame(window, [since] {
                trace("show to frame", since.elapsed());
                traceEpoch("frame");
            });
        present();
    });
    QObject::connect(&instance, &Instance::requestReceived, window,
                     [present, applyRequest](const std::optional<Context> &context, const Instance::Request &req) {
                         applyRequest(context, req);
                         present();
                     });

    // Created but not shown, so the first toggle pays for as little as possible.
    if (cli.mode == ParsedCli::BackgroundGui) {
        window->create();
    } else if (cli.mode == ParsedCli::OpenGui) {
        applyRequest(initial, initialRequest);
        present();
    } else {
        backend.applyContext(initial);
        present();
    }

    return app.exec();
}
