#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>
#include <utility>

#include "attachmentsmodel.h"
#include "filepicker.h"
#include "messagesmodel.h"
#include "settings.h"

// Empty fields resolve to the real locations: fx from PATH then
// ~/.local/bin, grim and slurp from PATH, the runtime dir under
// $XDG_RUNTIME_DIR, and no settings or custom commands persistence unless
// passed explicitly by main or tests.
struct BackendPaths {
    QString grim, runtimeDir, settingsFile, slurp, commandsFile;
    int captureTimeoutMs = 5000;
    int modelsTimeoutMs = 8000;

    BackendPaths(QString grim = {}, QString runtimeDir = {}, QString settingsFile = {},
                 QString slurp = {}, QString commandsFile = {},
                 int captureTimeoutMs = 5000, int modelsTimeoutMs = 8000)
        : grim(std::move(grim)), runtimeDir(std::move(runtimeDir)), settingsFile(std::move(settingsFile)),
          slurp(std::move(slurp)), commandsFile(std::move(commandsFile)),
          captureTimeoutMs(captureTimeoutMs), modelsTimeoutMs(modelsTimeoutMs) {}

    BackendPaths withSettings(const QString &file) const { BackendPaths copy = *this; copy.settingsFile = file; return copy; }
    BackendPaths withCommands(const QString &file) const { BackendPaths copy = *this; copy.commandsFile = file; return copy; }
};

// One chat with `fx ask`. Each chat owns a fresh workspace directory because
// fx scopes `--resume last` to the working directory.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(AttachmentsModel *attachments READ attachments CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY canResumeChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString modelLabel READ modelLabel NOTIFY modelChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList modelEntries READ modelEntries NOTIFY modelsChanged)
    Q_PROPERTY(ModelsState modelsState READ modelsState NOTIFY modelsChanged)
    Q_PROPERTY(QString effort READ effort WRITE setEffort NOTIFY effortChanged)
    Q_PROPERTY(QStringList availableEfforts READ availableEfforts NOTIFY effortChanged)
    Q_PROPERTY(bool fast READ fast WRITE setFast NOTIFY fastChanged)
    Q_PROPERTY(bool supportsFast READ supportsFast NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList recentChats READ recentChats NOTIFY historyChanged)

public:
    enum Status { Idle, Running, Failed, Stopped };
    Q_ENUM(Status)
    enum ModelsState { ModelsIdle, ModelsLoading, ModelsReady, ModelsFailed };
    Q_ENUM(ModelsState)

    using Paths = BackendPaths;

    // An empty fxPath is resolved from PATH, then ~/.local/bin; an empty
    // stateDir means ~/.local/state/ask.
    explicit Backend(const QString &fxPath = {}, const QString &stateDir = {}, const Paths &paths = {},
                     QObject *parent = nullptr);
    ~Backend() override;

    MessagesModel *messages() { return &m_messages; }
    AttachmentsModel *attachments() { return &m_attachments; }
    Status status() const { return m_status; }
    QString errorText() const { return m_errorText; }
    bool canResume() const { return m_canResume; }
    QString workspace() const { return m_workspace; }
    QString stateDir() const { return m_stateDir; }
    QString model() const { return m_model; }
    QString modelLabel() const { return m_model.isEmpty() ? QString() : shortModelName(m_model); }
    QStringList models() const { return m_models; }
    const QList<ModelInfo> &modelInfos() const { return m_modelInfos; }
    QVariantList modelEntries() const;
    ModelsState modelsState() const { return m_modelsState; }
    QString effort() const { return m_effort; }
    QStringList availableEfforts() const;
    bool fast() const { return m_fast; }
    bool supportsFast() const;
    QString runtimeDir() const { return m_runtimeDir; }
    QString uiLanguage() const { return m_uiLanguage; }

    void setFilePicker(std::unique_ptr<FilePicker> picker);
    Q_INVOKABLE void setUiLanguage(const QString &lang);

    // False when the question was not accepted (blank, a request is running,
    // or an enabled chip is still capturing), so the caller keeps the text.
    Q_INVOKABLE bool send(const QString &prompt);
    Q_INVOKABLE bool runCommand(const QString &id);
    Q_INVOKABLE QVariantList commands() const;
    Q_INVOKABLE QVariantList commandsForLanguage(const QString &language) const;
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void copyLastAnswer();
    Q_INVOKABLE void copyLastQuestion();
    Q_INVOKABLE bool copyLastCodeBlock();
    Q_INVOKABLE bool hasLastCodeBlock() const;
    Q_INVOKABLE QString lastQuestion() const;
    Q_INVOKABLE bool regenerate();
    Q_INVOKABLE QString editLastQuestion();

    // The hotkey client's context, applied before the window shows.
    void applyContext(const std::optional<Context> &context) { release(m_attachments.applyContext(context)); }
    Q_INVOKABLE void toggleAttachment(int row);
    Q_INVOKABLE void toggleScreen();
    Q_INVOKABLE void removeAttachment(int row);
    Q_INVOKABLE bool removeLastAttachment();
    Q_INVOKABLE bool pasteImage();
    Q_INVOKABLE bool attachClipboardText();
    Q_INVOKABLE int addImageFiles(const QList<QUrl> &urls);
    Q_INVOKABLE int addFiles(const QList<QUrl> &urls);
    Q_INVOKABLE void pickFiles();
    Q_INVOKABLE void captureRegion();

    Q_INVOKABLE void loadModels();
    Q_INVOKABLE void setModel(const QString &id);
    Q_INVOKABLE void setEffort(const QString &effort);
    Q_INVOKABLE void setFast(bool fast);

    QVariantList recentChats() const;
    Q_INVOKABLE bool loadChat(const QString &id);
    Q_INVOKABLE bool deleteChat(const QString &id);

signals:
    void statusChanged();
    void canResumeChanged();
    void modelChanged();
    void modelsChanged();
    void effortChanged();
    void fastChanged();
    void historyChanged();
    void hideWindowRequested();
    void showWindowRequested();

private:
    void setStatus(Status status, const QString &error = {});
    bool ensureWorkspace();
    void flushPendingOutput();
    void finished(QProcess *process, int code, QProcess::ExitStatus exit, const QByteArray &errors);
    static QString failureText(int code, const QByteArray &stderrBytes);
    bool ensureRuntimeDir();
    QString newRuntimeFile(const QString &stem, const QString &suffix);
    void startCapture(int row);
    void startRegionCapture();
    void restoreInFlight();
    void release(const QList<Attachment> &attachments);
    void saveSettings() const;

    QString m_fxPath, m_stateDir, m_workspace, m_grim, m_slurp, m_runtimeDir, m_settingsFile;
    Paths m_paths;
    MessagesModel m_messages;
    AttachmentsModel m_attachments;
    QList<Attachment> m_inFlightAttachments;
    std::unique_ptr<FilePicker> m_filePicker;
    int m_fileCounter = 0;
    Status m_status = Idle;
    QString m_errorText;
    bool m_canResume = false;
    bool m_capturingRegion = false;
    mutable bool m_prunedOnStartup = false;
    QString m_model;
    QString m_effort;
    bool m_fast = false;
    QString m_uiLanguage;
    mutable QString m_lastQueriedLanguage;
    QStringList m_models;
    QList<ModelInfo> m_modelInfos;
    ModelsState m_modelsState = ModelsIdle;
    QPointer<QProcess> m_process;
    QString m_pendingOutput;
    QTimer m_flushTimer;
    QStringDecoder m_decoder{QStringConverter::Utf8};
};
