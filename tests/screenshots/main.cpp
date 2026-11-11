// Renders the real QML offscreen with made-up content and saves PNGs for the README.
// Usage: ask_screenshots <scenario> <output.png>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>

#include "backend.h"
#include "theme.h"
#include "translator.h"

namespace {
void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

const char *kSelection =
    "The committee agreed to postpone the migration until the second quarter, pending a security review of the new authentication flow.";
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc < 3)
        return 2;
    const QString scenario = argv[1];
    QQuickStyle::setStyle("Basic");

    QTemporaryDir scratch;
    Translator translator(scratch.filePath("language"), scratch.path());
    app.installTranslator(&translator);
    // A missing theme directory means the default accent; no hyprctl means square corners.
    Theme theme(scratch.filePath("theme"), nullptr, "/nonexistent/hyprctl");
    Backend backend(QString(SHOTS_DIR) + "/fx", scratch.filePath("state"),
                    {QString(SHOTS_DIR) + "/grim", scratch.filePath("run"), {}});
    backend.setUiLanguage(translator.language());
    qmlRegisterSingletonType(QUrl("qrc:/Palette.qml"), "Ask", 1, 0, "Palette");
    qmlRegisterUncreatableType<Backend>("Ask", 1, 0, "Backend", "provided by the application");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window)
        return 1;

    const bool withSelection = scenario != "empty";
    backend.applyContext(Context{"org.mozilla.firefox", "Quarterly planning notes",
                                 scenario == "empty" ? "" : "180000a8", withSelection ? kSelection : ""});
    window->show();
    auto *input = window->findChild<QObject *>("input");

    if (scenario == "screen") {
        backend.toggleScreen();
        wait(500);
        input->setProperty("text", "What did they decide, and why?");
    } else if (scenario == "models") {
        QMetaObject::invokeMethod(window, "openModels");
        wait(800);
    } else if (scenario == "answer") {
        backend.toggleScreen();
        wait(300);
        backend.send("What did they decide, and why?");
        for (int i = 0; i < 100 && backend.status() == Backend::Running; ++i)
            wait(50);
    }
    wait(500);
    window->grabWindow().save(argv[2]);
    return 0;
}
