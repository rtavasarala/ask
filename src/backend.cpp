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
#include <QSet>
#include <QWindow>
#include <unistd.h>
#include <utility>

#include "commands.h"
#include "history.h"
#include "portalfilepicker.h"
#include "prompt.h"

namespace {
bool isEnglishCode(const QString &language) {
    const QString clean = language.trimmed().toLower();
    return clean.isEmpty() || clean == QLatin1String("en")
           || clean.startsWith(QLatin1String("en-")) || clean.startsWith(QLatin1String("en_"));
}
}

Backend::Backend(const QString &fxPath, const QString &stateDir, const Paths &paths, QObject *parent)
    : QObject(parent),
      m_fxPath(fxPath),
      m_stateDir(stateDir.isEmpty() ? QDir::homePath() + "/.local/state/ask" : stateDir),
      m_grim(paths.grim.isEmpty() ? QStringLiteral("grim") : paths.grim),
      m_slurp(paths.slurp.isEmpty() ? QStringLiteral("slurp") : paths.slurp),
      m_runtimeDir(paths.runtimeDir),
      m_settingsFile(paths.settingsFile),
      m_paths(paths) {
    if (m_runtimeDir.isEmpty()) {
        const QString base = qEnvironmentVariable("XDG_RUNTIME_DIR", QString("/tmp/ask-%1").arg(getuid()));
        m_runtimeDir = base + "/ask";
    }
    if (!m_settingsFile.isEmpty()) {
        const Settings loaded = Settings::load(m_settingsFile);
        m_model = loaded.model;
        m_effort = loaded.effort;
        m_fast = loaded.fast;
    }
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(16);
    connect(&m_flushTimer, &QTimer::timeout, this, &Backend::flushPendingOutput);
}

Backend::~Backend() {
    m_flushTimer.stop();
    for (auto *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
    release(m_attachments.clear());
    release(std::exchange(m_inFlightAttachments, {}));
}

void Backend::setFilePicker(std::unique_ptr<FilePicker> picker) {
    m_filePicker = std::move(picker);
}

void Backend::setUiLanguage(const QString &lang) {
    m_uiLanguage = lang.trimmed();
}

void Backend::saveSettings() const {
    if (!m_settingsFile.isEmpty())
        Settings{m_model, m_effort, m_fast}.save(m_settingsFile);
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

void Backend::restoreInFlight() {
    const QList<Attachment> inFlight = std::exchange(m_inFlightAttachments, {});
    if (inFlight.isEmpty())
        return;
    bool hasSelection = false;
    for (const Attachment &existing : m_attachments.items()) {
        if (existing.kind == Attachment::Selection)
            hasSelection = true;
    }
    const bool hasScreen = m_attachments.hasScreen();
    QList<Attachment> keep, drop;
    for (const Attachment &a : inFlight) {
        if ((a.kind == Attachment::Selection && hasSelection)
            || (a.kind == Attachment::Screen && hasScreen)) {
            drop << a;
        } else {
            keep << a;
        }
    }
    release(drop);
    m_attachments.restore(keep);
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
    if (!m_prunedOnStartup) {
        m_prunedOnStartup = true;
        History::prune(m_stateDir, m_workspace);
    }
    return true;
}

void Backend::flushPendingOutput() {
    m_flushTimer.stop();
    if (!m_pendingOutput.isEmpty())
        m_messages.appendToLast(std::exchange(m_pendingOutput, {}));
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
    const QString fx = m_fxPath.isEmpty() ? resolveFxExecutable() : m_fxPath;
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

    // The chips this question consumed. If the request fails, they return to the model.
    const QList<Attachment> taken = m_attachments.takeEnabled();
    const AssembledPrompt assembled = assemblePrompt(text, taken);
    release(std::exchange(m_inFlightAttachments, taken));

    const ModelInfo *matchedModel = nullptr;
    bool richMetadata = false;
    for (const ModelInfo &info : m_modelInfos) {
        if (!info.name.isEmpty() || !info.efforts.isEmpty() || info.fast || info.ultrafast)
            richMetadata = true;
        if (!m_model.isEmpty() && info.id == m_model)
            matchedModel = &info;
    }

    QStringList args{"ask", "--no-color", "--system", askSystemPrompt()};
    if (!m_model.isEmpty())
        args << "--model" << m_model;
    if (!m_effort.isEmpty() && (!richMetadata || !matchedModel || matchedModel->efforts.contains(m_effort)))
        args << "--effort" << m_effort;
    if (m_fast && (!richMetadata || !matchedModel || matchedModel->fast || matchedModel->ultrafast))
        args << "--fast";
    if (m_canResume)
        args << "--resume" << "last";
    for (const QString &image : assembled.images)
        args << "--image" << image;
    args << "--" << assembled.text;

    m_pendingOutput.clear();
    m_flushTimer.stop();
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
        if (process != m_process || bytes.isEmpty())
            return;
        m_pendingOutput += m_decoder(bytes);
        if (!m_flushTimer.isActive())
            m_flushTimer.start();
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] { *errors += process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != m_process)
            return;
        flushPendingOutput();
        m_process.clear();
        m_messages.setLastState("failed");
        setStatus(Failed, tr("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
        restoreInFlight();
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
    const QByteArray rest = process->readAllStandardOutput();
    if (!rest.isEmpty())
        m_pendingOutput += m_decoder(rest);
    flushPendingOutput();

    if (exit == QProcess::NormalExit && code == 0) {
        release(std::exchange(m_inFlightAttachments, {}));
        m_messages.setLastState("done");
        if (!m_canResume) {
            m_canResume = true;
            emit canResumeChanged();
        }
        History::recordChat(m_stateDir, m_workspace, m_model, m_messages.turns());
        m_prunedOnStartup = true;
        emit historyChanged();
        setStatus(Idle);
    } else {
        restoreInFlight();
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
    const QByteArray buffered = process->readAllStandardOutput();
    if (!buffered.isEmpty())
        m_pendingOutput += m_decoder(buffered);
    flushPendingOutput();

    // The process is detached from our state now; it only has to die.
    process->terminate();
    QTimer::singleShot(2000, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    release(std::exchange(m_inFlightAttachments, {}));
    m_messages.setLastState("stopped");
    setStatus(Stopped);
}

void Backend::newChat() {
    if (m_status == Running)
        stop();
    m_pendingOutput.clear();
    m_flushTimer.stop();
    release(std::exchange(m_inFlightAttachments, {}));
    m_messages.clear();
    m_workspace.clear();
    release(m_attachments.clear());
    if (m_canResume) {
        m_canResume = false;
        emit canResumeChanged();
    }
    setStatus(Idle);
    emit historyChanged();
}

QString Backend::lastQuestion() const { return m_messages.lastText("user"); }

void Backend::copyText(const QString &text) {
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void Backend::copyLastAnswer() {
    copyText(m_messages.lastText("assistant"));
}

void Backend::copyLastQuestion() {
    copyText(lastQuestion());
}

bool Backend::copyLastCodeBlock() {
    const QString code = m_messages.lastCodeBlock();
    if (code.isEmpty())
        return false;
    QGuiApplication::clipboard()->setText(code);
    return true;
}

bool Backend::hasLastCodeBlock() const {
    return m_messages.hasLastCodeBlock();
}

bool Backend::regenerate() {
    if (m_status == Running || m_messages.count() < 2 || lastQuestion().trimmed().isEmpty())
        return false;
    for (const Attachment &a : m_attachments.items())
        if (a.enabled && a.state == Attachment::Capturing)
            return false;
    const QString q = m_messages.removeLastTurn();
    if (m_messages.count() == 0 && m_canResume) {
        m_canResume = false;
        emit canResumeChanged();
    }
    return send(q);
}

QString Backend::editLastQuestion() {
    if (m_status == Running)
        stop();
    if (m_messages.count() == 0)
        return {};
    const QString q = m_messages.removeLastTurn();
    if (m_messages.count() == 0 && m_canResume) {
        m_canResume = false;
        emit canResumeChanged();
    }
    setStatus(Idle);
    return q;
}

bool Backend::runCommand(const QString &id) {
    if (id == QLatin1String("translateToLocal")) {
        if (!m_attachments.hasEnabledSelection())
            return false;
        const QString lang = !m_uiLanguage.isEmpty() ? m_uiLanguage : m_lastQueriedLanguage;
        if (isEnglishCode(lang))
            return false;
        const QString target = translateTargetLanguageName(lang);
        if (target.isEmpty())
            return false;
        return send(QString("Translate the selected text to %1. Reply with only the translation.").arg(target));
    }
    if (const AiCommand *command = findAiCommand(id)) {
        if (command->requiresSelection && !m_attachments.hasEnabledSelection())
            return false;
        return send(command->promptTemplate);
    }
    if (!m_paths.commandsFile.isEmpty()) {
        for (const AiCommand &custom : loadCustomCommands(m_paths.commandsFile)) {
            if (custom.id != id)
                continue;
            if (custom.requiresSelection && !m_attachments.hasEnabledSelection())
                return false;
            return send(custom.promptTemplate);
        }
    }
    return false;
}

QVariantList Backend::commands() const {
    return commandsForLanguage({});
}

QVariantList Backend::commandsForLanguage(const QString &language) const {
    if (!language.isEmpty())
        m_lastQueriedLanguage = language.trimmed();

    QVariantList list;
    QSet<QString> seen;
    const QString targetLang = isEnglishCode(language) ? QString() : translateTargetLanguageName(language);

    for (const AiCommand &command : aiCommands()) {
        seen.insert(command.id);
        list << QVariantMap{
            {"id", command.id},
            {"title", command.title ? QString::fromUtf8(command.title) : command.customTitle},
            {"requiresSelection", command.requiresSelection},
            {"isCustom", false},
            {"custom", false},
        };
        if (command.id == QLatin1String("translate") && !targetLang.isEmpty() && targetLang != QLatin1String("English")) {
            seen.insert(QStringLiteral("translateToLocal"));
            list << QVariantMap{
                {"id", QStringLiteral("translateToLocal")},
                {"title", QStringLiteral("Translate to %1").arg(targetLang)},
                {"targetLanguage", targetLang},
                {"requiresSelection", true},
                {"isCustom", false},
                {"custom", false},
            };
        }
    }

    if (!m_paths.commandsFile.isEmpty()) {
        for (const AiCommand &custom : loadCustomCommands(m_paths.commandsFile)) {
            if (custom.id.isEmpty() || seen.contains(custom.id))
                continue;
            seen.insert(custom.id);
            list << QVariantMap{
                {"id", custom.id},
                {"title", custom.title ? QString::fromUtf8(custom.title) : custom.customTitle},
                {"requiresSelection", custom.requiresSelection},
                {"isCustom", true},
                {"custom", true},
            };
        }
    }
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
    auto settled = std::make_shared<bool>(false);
    auto fail = [this, id, target, process, settled](const QString &why) {
        if (std::exchange(*settled, true))
            return;
        QFile::remove(target);
        const int at = m_attachments.rowOfId(id);
        if (at >= 0) {
            m_attachments.setEnabled(at, false);
            m_attachments.setState(at, Attachment::Failed, why);
        }
        process->deleteLater();
    };

    if (m_paths.captureTimeoutMs > 0) {
        QTimer::singleShot(m_paths.captureTimeoutMs, process, [this, process, fail] {
            if (process->state() == QProcess::NotRunning)
                return;
            process->disconnect(this);
            process->kill();
            fail(tr("Window capture timed out."));
        });
    }

    connect(process, &QProcess::readyReadStandardError, this, [process, errors] { *errors += process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [fail, this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("grim is not installed or not on PATH."));
    });
    connect(process, &QProcess::finished, this, [this, id, target, process, errors, settled, fail](int code, QProcess::ExitStatus exit) {
        if (*settled)
            return;
        if (exit != QProcess::NormalExit || code != 0 || !QFileInfo(target).size()) {
            const QString detail = QString::fromUtf8(*errors).trimmed();
            fail(detail.isEmpty() ? tr("grim exited with code %1").arg(code) : detail.left(300));
            return;
        }
        *settled = true;
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

void Backend::captureRegion() {
    if (m_capturingRegion)
        return;
    m_capturingRegion = true;

    bool hadVisibleWindow = false;
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (w && w->isVisible()) {
            hadVisibleWindow = true;
            break;
        }
    }
    const QString platform = QGuiApplication::platformName();
    const bool headless = platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");

    emit hideWindowRequested();
    if (hadVisibleWindow && !headless) {
        QTimer::singleShot(120, this, &Backend::startRegionCapture);
    } else {
        startRegionCapture();
    }
}

void Backend::startRegionCapture() {
    auto *slurpProc = new QProcess(this);
    auto slurpSettled = std::make_shared<bool>(false);

    connect(slurpProc, &QProcess::errorOccurred, this, [this, slurpProc, slurpSettled](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || std::exchange(*slurpSettled, true))
            return;
        m_capturingRegion = false;
        setStatus(Failed, tr("slurp is not installed or not on PATH."));
        emit showWindowRequested();
        slurpProc->deleteLater();
    });

    connect(slurpProc, &QProcess::finished, this, [this, slurpProc, slurpSettled](int code, QProcess::ExitStatus exit) {
        if (std::exchange(*slurpSettled, true))
            return;
        const QString geom = QString::fromUtf8(slurpProc->readAllStandardOutput()).trimmed();
        slurpProc->deleteLater();

        if (exit != QProcess::NormalExit || code != 0 || geom.isEmpty()) {
            m_capturingRegion = false;
            emit showWindowRequested();
            return;
        }

        const QString target = newRuntimeFile("region", "jpg");
        if (target.isEmpty()) {
            m_capturingRegion = false;
            setStatus(Failed, tr("Could not create %1.").arg(m_runtimeDir));
            emit showWindowRequested();
            return;
        }

        auto *grimProc = new QProcess(this);
        auto errors = std::make_shared<QByteArray>();
        auto grimSettled = std::make_shared<bool>(false);
        auto failGrim = [this, target, grimProc, grimSettled](const QString &why) {
            if (std::exchange(*grimSettled, true))
                return;
            QFile::remove(target);
            m_capturingRegion = false;
            setStatus(Failed, why);
            emit showWindowRequested();
            grimProc->deleteLater();
        };

        if (m_paths.captureTimeoutMs > 0) {
            QTimer::singleShot(m_paths.captureTimeoutMs, grimProc, [this, grimProc, failGrim] {
                if (grimProc->state() == QProcess::NotRunning)
                    return;
                grimProc->disconnect(this);
                grimProc->kill();
                failGrim(tr("Window capture timed out."));
            });
        }

        connect(grimProc, &QProcess::readyReadStandardError, this,
                [grimProc, errors] { *errors += grimProc->readAllStandardError(); });
        connect(grimProc, &QProcess::errorOccurred, this, [this, failGrim](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                failGrim(tr("grim is not installed or not on PATH."));
        });
        connect(grimProc, &QProcess::finished, this,
                [this, target, grimProc, errors, grimSettled, failGrim](int gCode, QProcess::ExitStatus gExit) {
            if (*grimSettled)
                return;
            if (gExit != QProcess::NormalExit || gCode != 0 || !QFileInfo(target).size()) {
                const QString detail = QString::fromUtf8(*errors).trimmed();
                failGrim(detail.isEmpty() ? tr("grim exited with code %1").arg(gCode) : detail.left(300));
                return;
            }
            *grimSettled = true;
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
            Attachment a;
            a.kind = Attachment::Image;
            a.label = tr("Region screenshot");
            a.payload = target;
            a.owned = true;
            m_attachments.add(a);
            m_capturingRegion = false;
            emit showWindowRequested();
            grimProc->deleteLater();
        });
        grimProc->start(m_grim, {"-g", geom, "-t", "jpeg", "-q", "85", target}, QIODevice::ReadOnly);
    });

    slurpProc->start(m_slurp, {}, QIODevice::ReadOnly);
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

bool Backend::attachClipboardText() {
    const QString raw = QGuiApplication::clipboard()->text();
    if (raw.trimmed().isEmpty())
        return false;
    const QString bounded = raw.left(Context::kMaxSelection);
    for (int i = m_attachments.rowCount() - 1; i >= 0; --i) {
        if (m_attachments.at(i)->kind == Attachment::Selection)
            release({*m_attachments.removeAt(i)});
    }
    Attachment a;
    a.kind = Attachment::Selection;
    a.label = oneLinePreview(bounded);
    a.tooltip = oneLinePreview(bounded, 400);
    a.payload = bounded;
    a.enabled = true;
    m_attachments.restore({a});
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

int Backend::addFiles(const QList<QUrl> &urls) {
    int added = 0;
    for (const QUrl &url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (const auto att = attachmentFromLocalFile(path)) {
            m_attachments.add(*att);
            ++added;
        }
    }
    return added;
}

void Backend::pickFiles() {
    if (!m_filePicker)
        m_filePicker = std::make_unique<PortalFilePicker>(this);
    QPointer<Backend> self(this);
    m_filePicker->openFiles(QGuiApplication::focusWindow(), [self](const QList<QUrl> &urls) {
        if (self && !urls.isEmpty())
            self->addFiles(urls);
    });
}

QVariantList Backend::modelEntries() const {
    QVariantList list;
    list.reserve(m_modelInfos.size());
    for (const ModelInfo &info : m_modelInfos) {
        list << QVariantMap{
            {"id", info.id},
            {"name", info.name},
            {"efforts", info.efforts},
            {"fast", info.fast},
            {"ultrafast", info.ultrafast},
        };
    }
    return list;
}

QStringList Backend::availableEfforts() const {
    bool richMetadata = false;
    for (const ModelInfo &info : m_modelInfos) {
        if (!info.name.isEmpty() || !info.efforts.isEmpty() || info.fast || info.ultrafast) {
            richMetadata = true;
            break;
        }
    }
    if (!m_model.isEmpty()) {
        for (const ModelInfo &info : m_modelInfos) {
            if (info.id == m_model)
                return richMetadata ? info.efforts : QStringList{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};
        }
    }
    return {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};
}

bool Backend::supportsFast() const {
    for (const ModelInfo &info : m_modelInfos) {
        if (info.id == m_model)
            return info.fast || info.ultrafast;
    }
    return false;
}

void Backend::loadModels() {
    if (m_modelsState == ModelsLoading || m_modelsState == ModelsReady)
        return;
    const QString fx = m_fxPath.isEmpty() ? resolveFxExecutable() : m_fxPath;
    m_modelsState = ModelsLoading;
    emit modelsChanged();

    auto *process = new QProcess(this);
    auto settled = std::make_shared<bool>(false);
    auto failModels = [this, process, settled] {
        if (std::exchange(*settled, true))
            return;
        m_modelInfos.clear();
        m_models.clear();
        m_modelsState = ModelsFailed;
        emit modelsChanged();
        emit effortChanged();
        process->deleteLater();
    };

    if (m_paths.modelsTimeoutMs > 0) {
        QTimer::singleShot(m_paths.modelsTimeoutMs, process, [this, process, failModels] {
            if (process->state() == QProcess::NotRunning)
                return;
            process->disconnect(this);
            process->kill();
            failModels();
        });
    }

    connect(process, &QProcess::errorOccurred, this, [failModels](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            failModels();
    });
    connect(process, &QProcess::finished, this, [this, process, settled, failModels](int code, QProcess::ExitStatus exit) {
        if (*settled)
            return;
        const QList<ModelInfo> parsed = parseModelInfos(process->readAllStandardOutput());
        if (exit != QProcess::NormalExit || code != 0 || parsed.isEmpty()) {
            failModels();
            return;
        }
        *settled = true;
        m_modelInfos = parsed;
        m_models.clear();
        m_models.reserve(parsed.size());
        for (const ModelInfo &info : parsed)
            m_models << info.id;
        m_modelsState = ModelsReady;
        emit modelsChanged();
        emit effortChanged();
        process->deleteLater();
    });
    process->start(fx.isEmpty() ? QStringLiteral("fx") : fx, {"models", "--json"}, QIODevice::ReadOnly);
}

void Backend::setModel(const QString &id) {
    if (id == m_model)
        return;
    m_model = id;
    saveSettings();
    emit modelChanged();
    emit modelsChanged();
    emit effortChanged();
}

void Backend::setEffort(const QString &effort) {
    const QString trimmed = effort.trimmed();
    if (trimmed == m_effort)
        return;
    m_effort = trimmed;
    saveSettings();
    emit effortChanged();
}

void Backend::setFast(bool fast) {
    if (fast == m_fast)
        return;
    m_fast = fast;
    saveSettings();
    emit fastChanged();
}

QVariantList Backend::recentChats() const {
    if (!m_prunedOnStartup) {
        m_prunedOnStartup = true;
        History::prune(m_stateDir, m_workspace);
    }
    const QString cleanActive = m_workspace.isEmpty() ? QString() : QDir::cleanPath(m_workspace);
    QVariantList list;
    for (const SavedChat &chat : History::load(m_stateDir)) {
        list << QVariantMap{
            {"id", chat.id},
            {"title", chat.title},
            {"model", chat.model},
            {"modelLabel", chat.model.isEmpty() ? QString() : shortModelName(chat.model)},
            {"updatedAt", chat.updatedAt},
            {"active", !cleanActive.isEmpty() && QDir::cleanPath(chat.workspace) == cleanActive},
            {"turnCount", int(chat.turns.size())},
        };
    }
    return list;
}

bool Backend::loadChat(const QString &id) {
    const auto saved = History::find(m_stateDir, id);
    if (!saved || !QFileInfo(saved->workspace).isDir())
        return false;
    if (m_status == Running)
        stop();
    m_pendingOutput.clear();
    m_flushTimer.stop();
    release(std::exchange(m_inFlightAttachments, {}));
    release(m_attachments.clear());
    m_workspace = saved->workspace;
    m_messages.setTurns(saved->turns);
    const bool resumable = !saved->turns.isEmpty();
    if (m_canResume != resumable) {
        m_canResume = resumable;
        emit canResumeChanged();
    }
    setStatus(Idle);
    emit historyChanged();
    return true;
}

bool Backend::deleteChat(const QString &id) {
    const auto saved = History::find(m_stateDir, id);
    if (!saved)
        return false;
    if (!m_workspace.isEmpty() && QDir::cleanPath(saved->workspace) == QDir::cleanPath(m_workspace))
        newChat();
    const bool ok = History::remove(m_stateDir, id);
    emit historyChanged();
    return ok;
}
