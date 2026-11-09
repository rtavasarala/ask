#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

#include "backend.h"
#include "instance.h"
#include "theme.h"
#include "translator.h"

namespace {

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

struct Rig {
    QTemporaryDir dir;
    QString fx = dir.filePath("fx");
    QString state = dir.filePath("state");
    Rig() {
        QVERIFY(QFile::copy(QString(TESTS_DIR) + "/fake_fx.sh", fx));
        QFile::setPermissions(fx, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QStringList calls() const {
        QFile file(dir.filePath("log"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    }
};

QString cwdOf(const QString &call) { return call.section('|', 0, 0); }
QString answer(Backend &b, int row = 1) { return b.messages()->index(row).data(MessagesModel::TextRole).toString(); }
QString stateOf(Backend &b, int row = 1) { return b.messages()->index(row).data(MessagesModel::StateRole).toString(); }
}

class AskTests : public QObject {
    Q_OBJECT

private slots:
    void streamingFillsOneAssistantRowAndEndsIdle() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("hi");
        QCOMPARE(backend.status(), Backend::Running);
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        QCOMPARE(backend.messages()->count(), 2);
        QCOMPARE(answer(backend), QString("Hello world"));
        QCOMPARE(stateOf(backend), QString("done"));
        QCOMPARE(backend.lastQuestion(), QString("hi"));
    }

    void textArrivesBeforeTheProcessEnds() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("hi");
        QTRY_VERIFY_WITH_TIMEOUT(answer(backend).startsWith("Hello"), 5000);
        QCOMPARE(backend.status(), Backend::Running);
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
    }

    void requestShape() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("  what is up  ");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        const QStringList args = rig.calls().first().split('|');
        QCOMPARE(args.mid(1, 3), (QStringList{"ask", "--no-color", "--system"}));
        QCOMPARE(args.at(args.size() - 2), QString("--"));
        QCOMPARE(args.last(), QString("what is up"));
        QVERIFY(!args.contains("--resume"));
        QVERIFY(QDir(cwdOf(rig.calls().first())).exists());
    }

    void blankAndConcurrentSendsAreIgnored() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("   ");
        QCOMPARE(backend.messages()->count(), 0);
        backend.send("one");
        backend.send("two");
        QCOMPARE(backend.messages()->count(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        QCOMPARE(rig.calls().size(), 1);
    }

    void followUpResumesInTheSameWorkspace() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("first");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        backend.send("second");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        const QStringList calls = rig.calls();
        QCOMPARE(calls.size(), 2);
        QVERIFY(calls.at(1).contains("|--resume|last|--|second"));
        QCOMPARE(cwdOf(calls.at(0)), cwdOf(calls.at(1)));
        QCOMPARE(backend.messages()->count(), 4);
    }

    void newChatUsesANewWorkspaceWithoutResume() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("first");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        backend.newChat();
        QCOMPARE(backend.messages()->count(), 0);
        QVERIFY(!backend.canResume());
        backend.send("fresh");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        const QStringList calls = rig.calls();
        QVERIFY(!calls.at(1).contains("--resume"));
        QVERIFY(cwdOf(calls.at(0)) != cwdOf(calls.at(1)));
    }

    void failureShowsStderrWithoutNotices() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("fail");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Failed, 5000);
        QVERIFY(backend.errorText().contains("boom: not signed in"));
        QVERIFY(!backend.errorText().contains("[notice]"));
        QCOMPARE(stateOf(backend), QString("failed"));
        QVERIFY(!backend.canResume());
        backend.send("again");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        QVERIFY(backend.errorText().isEmpty());
    }

    void failureWithoutStderrNamesTheExitCode() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("quiet");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Failed, 5000);
        QVERIFY(backend.errorText().contains("3"));
    }

    void stopKeepsThePartialAnswer() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("slow");
        QTRY_VERIFY_WITH_TIMEOUT(answer(backend) == "part", 5000);
        backend.stop();
        QCOMPARE(backend.status(), Backend::Stopped);
        QCOMPARE(answer(backend), QString("part"));
        QCOMPARE(stateOf(backend), QString("stopped"));
        QVERIFY(!backend.canResume());
        backend.send("after");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        QCOMPARE(answer(backend, 3), QString("Hello world"));
        QCOMPARE(answer(backend, 1), QString("part"));
    }

    void multiByteCharactersSurviveChunkBoundaries() {
        Rig rig;
        Backend backend(rig.fx, rig.state);
        backend.send("utf8");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Idle, 5000);
        QCOMPARE(answer(backend), QString("café ok"));
    }

    void missingFxFailsWithInstallHelp() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("no-such-fx"), dir.filePath("state"));
        backend.send("hi");
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), Backend::Failed, 5000);
        QVERIFY(backend.errorText().contains("https://fx.sh"));
        QCOMPARE(stateOf(backend), QString("failed"));
    }

    void sanitizerReplacesImagesAndStripsHtml() {
        QCOMPARE(MessagesModel::sanitize("see ![a cat](https://evil.example/x.png) here"), QString("see a cat here"));
        QCOMPARE(MessagesModel::sanitize("![ref img][1]\n\n[1]: https://evil.example/y.png"), QString("ref img\n\n[1]: https://evil.example/y.png"));
        QCOMPARE(MessagesModel::sanitize("a <img src=\"https://evil.example/z.png\" onerror=x> b"), QString("a  b"));
        QCOMPARE(MessagesModel::sanitize("a <IMG src=x"), QString("a "));
        QCOMPARE(MessagesModel::sanitize("[link](https://ok.example) and `code`"), QString("[link](https://ok.example) and `code`"));
    }

    void renderedTextNeverContainsImagesWhileStreaming() {
        MessagesModel model;
        model.append("assistant", {}, "streaming");
        model.appendToLast("hi ![alt](http://x");
        model.appendToLast(".example/p.png) there");
        QCOMPARE(model.index(0).data(MessagesModel::DisplayRole).toString(), QString("hi alt there"));
        QCOMPARE(model.index(0).data(MessagesModel::TextRole).toString(), QString("hi ![alt](http://x.example/p.png) there"));
    }

    void secondLaunchTogglesTheRunningInstance() {
        const QString name = QString("ask-test-%1").arg(getpid());
        Instance running(name);
        QVERIFY(running.listen());
        QSignalSpy spy(&running, &Instance::toggleRequested);
        Instance second(name);
        QVERIFY(second.notifyRunning());
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(second.notifyRunning("ping\n"));
        QTest::qWait(100);
        QCOMPARE(spy.count(), 1);
    }

    void noRunningInstanceMeansNoOneToNotify() {
        Instance nobody(QString("ask-test-none-%1").arg(getpid()));
        QVERIFY(!nobody.notifyRunning());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", "{\"Save\": \"Guardar\", \"Open\": \"Abrir\"}");
        write(dir.path() + "/pt-BR.json", "{\"Save\": \"Salvar\"}");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", "{\"%n clips\": {\"one\": \"%n clip\", \"other\": \"%n clips\"}}");
        write(dir.path() + "/ru.json", "{\"%n clips\": {\"one\": \"%n клип\", \"few\": \"%n клипа\", \"many\": \"%n клипов\", \"other\": \"%n клипа\"}}");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void danishCatalogCoversEveryUiString() {
        QFile source(QString(SRC_DIR) + "/i18n/da.json");
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto catalog = QJsonDocument::fromJson(source.readAll()).object();
        QRegularExpression call("(?:qsTr|tr)\\(\"((?:[^\"\\\\]|\\\\.)*)\"");
        QStringList missing;
        for (const QString &name : QDir(SRC_DIR).entryList({"*.qml", "backend.cpp"})) {
            QFile file(QString(SRC_DIR) + "/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto it = call.globalMatch(QString::fromUtf8(file.readAll()));
            while (it.hasNext()) {
                const QString text = it.next().captured(1);
                if (!catalog.contains(text))
                    missing << text;
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(" | ")));
    }
};

QTEST_MAIN(AskTests)
#include "ask_tests.moc"
