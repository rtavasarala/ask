#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace {
const char *const kSystemPrompt =
    "You are Ask, a quick assistant in a desktop popup. Answer directly and concisely in Markdown. "
    "There is no project or file context; don't run tools or commands unless the user asks.";

// The launcher may not have ~/.local/bin on PATH, where fx installs itself.
QString resolveFx() {
    const QString found = QStandardPaths::findExecutable("fx");
    if (!found.isEmpty())
        return found;
    const QString fallback = QDir::homePath() + "/.local/bin/fx";
    return QFileInfo(fallback).isExecutable() ? fallback : QString();
}
}

Backend::Backend(const QString &fxPath, const QString &stateDir, QObject *parent)
    : QObject(parent), m_fxPath(fxPath), m_stateDir(stateDir.isEmpty() ? QDir::homePath() + "/.local/state/ask" : stateDir) {}

Backend::~Backend() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
}

void Backend::setStatus(Status status, const QString &error) {
    if (status == m_status && error == m_errorText)
        return;
    m_status = status;
    m_errorText = error;
    emit statusChanged();
}

bool Backend::ensureWorkspace() {
    if (!m_workspace.isEmpty())
        return true;
    const QString base = m_stateDir + "/chats/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    QString path = base;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = base + "-" + QString::number(n);
    if (!QDir().mkpath(path))
        return false;
    m_workspace = path;
    return true;
}

void Backend::send(const QString &prompt) {
    const QString text = prompt.trimmed();
    if (m_status == Running || text.isEmpty())
        return;

    m_messages.append("user", text);
    m_messages.append("assistant", {}, "streaming");

    // Resolved at send time so installing fx while Ask is resident works.
    const QString fx = m_fxPath.isEmpty() ? resolveFx() : m_fxPath;
    if (fx.isEmpty()) {
        m_messages.setLastState("failed");
        setStatus(Failed, tr("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
        return;
    }
    if (!ensureWorkspace()) {
        m_messages.setLastState("failed");
        setStatus(Failed, tr("Could not create a working directory in %1.").arg(m_stateDir));
        return;
    }

    QStringList args{"ask", "--no-color", "--system", kSystemPrompt};
    if (m_canResume)
        args << "--resume" << "last";
    args << "--" << text;

    auto *process = new QProcess(this);
    m_process = process;
    m_decoder = QStringDecoder(QStringConverter::Utf8);
    process->setWorkingDirectory(m_workspace);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("NO_COLOR", "1");
    process->setProcessEnvironment(environment);

    auto errors = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const QByteArray bytes = process->readAllStandardOutput();
        if (process == m_process)
            m_messages.appendToLast(m_decoder(bytes));
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] { *errors += process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != m_process)
            return;
        m_process.clear();
        m_messages.setLastState("failed");
        setStatus(Failed, tr("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process, errors](int code, QProcess::ExitStatus exit) {
        finished(process, code, exit, *errors);
    });

    setStatus(Running);
    process->start(fx, args, QIODevice::ReadOnly);
}

void Backend::finished(QProcess *process, int code, QProcess::ExitStatus exit, const QByteArray &errors) {
    process->deleteLater();
    if (process != m_process)
        return;
    m_process.clear();
    const QByteArray rest = process->readAllStandardOutput();
    if (!rest.isEmpty())
        m_messages.appendToLast(m_decoder(rest));
    if (exit == QProcess::NormalExit && code == 0) {
        m_messages.setLastState("done");
        if (!m_canResume) {
            m_canResume = true;
            emit canResumeChanged();
        }
        setStatus(Idle);
    } else {
        m_messages.setLastState("failed");
        setStatus(Failed, failureText(code, errors));
    }
}

QString Backend::failureText(int code, const QByteArray &stderrBytes) {
    QStringList lines;
    for (const QString &line : QString::fromUtf8(stderrBytes).split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith("[notice]"))
            lines << trimmed;
    }
    QString text = lines.mid(qMax(0, lines.size() - 4)).join('\n');
    if (text.size() > 500)
        text = "…" + text.right(499);
    return text.isEmpty() ? tr("fx exited with code %1").arg(code) : text;
}

void Backend::stop() {
    if (m_status != Running || !m_process)
        return;
    QProcess *process = m_process;
    m_process.clear();
    // The process is detached from our state now; it only has to die.
    process->terminate();
    QTimer::singleShot(2000, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    m_messages.setLastState("stopped");
    setStatus(Stopped);
}

void Backend::newChat() {
    if (m_status == Running)
        stop();
    m_messages.clear();
    m_workspace.clear();
    if (m_canResume) {
        m_canResume = false;
        emit canResumeChanged();
    }
    setStatus(Idle);
}

QString Backend::lastQuestion() const { return m_messages.lastText("user"); }

void Backend::copyLastAnswer() {
    const QString text = m_messages.lastText("assistant");
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void Backend::copyLastQuestion() {
    const QString text = lastQuestion();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}
