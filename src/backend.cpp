#include "backend.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
#include <unistd.h>

#include "commands.h"
#include "prompt.h"
#include "settings.h"

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

Backend::Backend(const QString &fxPath, const QString &stateDir, const Paths &paths, QObject *parent)
    : QObject(parent),
      m_fxPath(fxPath),
      m_stateDir(stateDir.isEmpty() ? QDir::homePath() + "/.local/state/ask" : stateDir),
      m_grim(paths.grim.isEmpty() ? QStringLiteral("grim") : paths.grim),
      m_runtimeDir(paths.runtimeDir),
      m_settingsFile(paths.settingsFile) {
    if (m_runtimeDir.isEmpty()) {
        const QString base = qEnvironmentVariable("XDG_RUNTIME_DIR", QString("/tmp/ask-%1").arg(getuid()));
        m_runtimeDir = base + "/ask";
    }
    if (!m_settingsFile.isEmpty())
        m_model = Settings::load(m_settingsFile).model;
}

Backend::~Backend() {
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
    release(m_attachments.clear());
    releaseInFlight();
}

bool Backend::ensureRuntimeDir() {
    if (!QDir().mkpath(m_runtimeDir))
        return false;
    return QFile::setPermissions(m_runtimeDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QString Backend::newRuntimeFile(const QString &stem, const QString &suffix) {
    if (!ensureRuntimeDir())
        return {};
    return QString("%1/%2-%3-%4.%5").arg(m_runtimeDir, stem).arg(getpid()).arg(++m_fileCounter).arg(suffix);
}

// Only files we made are deleted; a dropped-in original is never touched.
void Backend::release(const QList<Attachment> &attachments) {
    for (const Attachment &a : attachments)
        if (a.owned && !a.payload.isEmpty())
            QFile::remove(a.payload);
}

void Backend::releaseInFlight() {
    for (const QString &path : std::exchange(m_inFlightFiles, {}))
        QFile::remove(path);
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

bool Backend::send(const QString &prompt) {
    const QString text = prompt.trimmed();
    if (m_status == Running || text.isEmpty())
        return false;
    for (const Attachment &a : m_attachments.items())
        if (a.enabled && a.state == Attachment::Capturing)
            return false;

    QStringList carried;
    for (const Attachment &a : m_attachments.items()) {
        if (!a.enabled || a.state != Attachment::Ready)
            continue;
        carried << (a.kind == Attachment::Selection ? tr("Selection")
                    : a.kind == Attachment::Screen  ? (a.app.isEmpty() ? tr("Active window") : tr("%1 window").arg(a.app))
                                                    : a.label);
    }
    m_messages.append("user", text, {}, carried.join(QStringLiteral("  ·  ")));
    m_messages.append("assistant", {}, "streaming");

    // Resolved at send time so installing fx while Ask is resident works.
    const QString fx = m_fxPath.isEmpty() ? resolveFx() : m_fxPath;
    if (fx.isEmpty()) {
        m_messages.setLastState("failed");
        setStatus(Failed, tr("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
        return true;
    }
    if (!ensureWorkspace()) {
        m_messages.setLastState("failed");
        setStatus(Failed, tr("Could not create a working directory in %1.").arg(m_stateDir));
        return true;
    }

    // The chips this question consumed. Their files live until the request ends.
    const QList<Attachment> taken = m_attachments.takeEnabled();
    const AssembledPrompt assembled = assemblePrompt(text, taken);
    for (const Attachment &a : taken)
        if (a.owned && !a.payload.isEmpty())
            m_inFlightFiles << a.payload;

    QStringList args{"ask", "--no-color", "--system", kSystemPrompt};
    if (!m_model.isEmpty())
        args << "--model" << m_model;
    if (m_canResume)
        args << "--resume" << "last";
    for (const QString &image : assembled.images)
        args << "--image" << image;
    args << "--" << assembled.text;

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
        releaseInFlight();
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process, errors](int code, QProcess::ExitStatus exit) {
        finished(process, code, exit, *errors);
    });

    setStatus(Running);
    process->start(fx, args, QIODevice::ReadOnly);
    return true;
}

void Backend::finished(QProcess *process, int code, QProcess::ExitStatus exit, const QByteArray &errors) {
    process->deleteLater();
    if (process != m_process)
        return;
    m_process.clear();
    releaseInFlight();
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
    releaseInFlight();
    m_messages.setLastState("stopped");
    setStatus(Stopped);
}

void Backend::newChat() {
    if (m_status == Running)
        stop();
    m_messages.clear();
    m_workspace.clear();
    release(m_attachments.clear());
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

bool Backend::runCommand(const QString &id) {
    const AiCommand *command = findAiCommand(id);
    if (!command || (command->requiresSelection && !m_attachments.hasEnabledSelection()))
        return false;
    return send(command->promptTemplate);
}

QVariantList Backend::commands() const {
    QVariantList list;
    for (const AiCommand &command : aiCommands())
        list << QVariantMap{{"id", command.id}, {"title", QString::fromUtf8(command.title)},
                            {"requiresSelection", command.requiresSelection}};
    return list;
}

void Backend::toggleAttachment(int row) {
    const Attachment *a = m_attachments.at(row);
    if (!a || a->state == Attachment::Capturing)
        return;
    // The capture is taken the first time the chip is switched on, and again
    // when a failed one is switched on.
    if (a->kind == Attachment::Screen && (a->state == Attachment::Failed || (!a->enabled && a->payload.isEmpty()))) {
        startCapture(row);
        return;
    }
    m_attachments.setEnabled(row, !a->enabled);
}

void Backend::toggleScreen() {
    const int row = m_attachments.screenRow();
    if (row >= 0)
        toggleAttachment(row);
}

void Backend::startCapture(int row) {
    const Attachment *chip = m_attachments.at(row);
    if (!chip)
        return;
    const qint64 id = chip->id;
    const QString target = newRuntimeFile("screen", "jpg");
    if (target.isEmpty()) {
        m_attachments.setEnabled(row, false);
        m_attachments.setState(row, Attachment::Failed, tr("Could not create %1.").arg(m_runtimeDir));
        return;
    }
    m_attachments.setEnabled(row, true);
    m_attachments.setState(row, Attachment::Capturing);

    auto *process = new QProcess(this);
    auto errors = std::make_shared<QByteArray>();
    auto fail = [this, id, target, process](const QString &why) {
        QFile::remove(target);
        const int at = m_attachments.rowOfId(id);
        if (at >= 0) {
            m_attachments.setEnabled(at, false);
            m_attachments.setState(at, Attachment::Failed, why);
        }
        process->deleteLater();
    };
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] { *errors += process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [fail, this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("grim is not installed or not on PATH."));
    });
    connect(process, &QProcess::finished, this, [this, id, target, process, errors, fail](int code, QProcess::ExitStatus exit) {
        if (exit != QProcess::NormalExit || code != 0 || !QFileInfo(target).size()) {
            const QString detail = QString::fromUtf8(*errors).trimmed();
            fail(detail.isEmpty() ? tr("grim exited with code %1").arg(code) : detail.left(300));
            return;
        }
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        const int at = m_attachments.rowOfId(id);
        if (at < 0) {
            QFile::remove(target);  // the chip was removed or replaced meanwhile
        } else {
            m_attachments.setPayload(at, target);
            m_attachments.setOwned(at, true);
            m_attachments.setState(at, Attachment::Ready);
        }
        process->deleteLater();
    });
    process->start(m_grim, {"-T", chip->stableId, "-t", "jpeg", "-q", "80", target}, QIODevice::ReadOnly);
}

void Backend::removeAttachment(int row) {
    if (const auto removed = m_attachments.removeAt(row))
        release({*removed});
}

bool Backend::removeLastAttachment() {
    const int last = m_attachments.rowCount() - 1;
    if (last < 0)
        return false;
    removeAttachment(last);
    return true;
}

bool Backend::pasteImage() {
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    // Text wins: copying text from an app that also offers an image must paste text.
    if (!mime || !mime->hasImage() || mime->hasText())
        return false;
    const QImage image = qvariant_cast<QImage>(mime->imageData());
    const QString path = newRuntimeFile("paste", "png");
    if (image.isNull() || path.isEmpty() || !image.save(path, "PNG"))
        return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    Attachment a;
    a.kind = Attachment::Image;
    a.label = tr("Pasted image");
    a.payload = path;
    a.owned = true;
    m_attachments.add(a);
    return true;
}

int Backend::addImageFiles(const QList<QUrl> &urls) {
    int added = 0;
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty() || !QFileInfo(path).isFile() || !QImageReader(path).canRead())
            continue;
        Attachment a;
        a.kind = Attachment::Image;
        a.label = QFileInfo(path).fileName();
        a.tooltip = path;
        a.payload = path;
        m_attachments.add(a);
        ++added;
    }
    return added;
}

void Backend::loadModels() {
    if (m_modelsState == ModelsLoading || m_modelsState == ModelsReady)
        return;
    const QString fx = m_fxPath.isEmpty() ? resolveFx() : m_fxPath;
    m_modelsState = ModelsLoading;
    emit modelsChanged();
    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_modelsState = ModelsFailed;
        emit modelsChanged();
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus exit) {
        const QStringList parsed = parseModels(process->readAllStandardOutput());
        const bool ok = exit == QProcess::NormalExit && code == 0 && !parsed.isEmpty();
        m_models = ok ? parsed : QStringList();
        m_modelsState = ok ? ModelsReady : ModelsFailed;
        emit modelsChanged();
        process->deleteLater();
    });
    process->start(fx.isEmpty() ? QStringLiteral("fx") : fx, {"models"}, QIODevice::ReadOnly);
}

void Backend::setModel(const QString &id) {
    if (id == m_model)
        return;
    m_model = id;
    if (!m_settingsFile.isEmpty())
        Settings{m_model}.save(m_settingsFile);
    emit modelChanged();
}
