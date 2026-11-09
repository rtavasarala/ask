#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringDecoder>
#include <QUrl>

#include "attachmentsmodel.h"
#include "messagesmodel.h"
#include "settings.h"

// One chat with `fx ask`. Each chat owns a fresh workspace directory because
// fx scopes `--resume last` to the working directory.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(AttachmentsModel *attachments READ attachments CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY canResumeChanged)
    Q_PROPERTY(QString model READ model NOTIFY modelChanged)
    Q_PROPERTY(QString modelLabel READ modelLabel NOTIFY modelChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(ModelsState modelsState READ modelsState NOTIFY modelsChanged)

public:
    enum Status { Idle, Running, Failed, Stopped };
    Q_ENUM(Status)
    enum ModelsState { ModelsIdle, ModelsLoading, ModelsReady, ModelsFailed };
    Q_ENUM(ModelsState)

    // Empty fields resolve to the real locations: fx from PATH then
    // ~/.local/bin, grim from PATH, the runtime dir under $XDG_RUNTIME_DIR, and
    // no settings persistence (main passes Settings::defaultPath()).
    struct Paths {
        QString grim, runtimeDir, settingsFile;
        Paths withSettings(const QString &file) const { Paths copy = *this; copy.settingsFile = file; return copy; }
    };

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
    QString model() const { return m_model; }
    QString modelLabel() const { return m_model.isEmpty() ? QString() : shortModelName(m_model); }
    QStringList models() const { return m_models; }
    ModelsState modelsState() const { return m_modelsState; }
    QString runtimeDir() const { return m_runtimeDir; }

    // False when the question was not accepted (blank, a request is running,
    // or an enabled chip is still capturing), so the caller keeps the text.
    Q_INVOKABLE bool send(const QString &prompt);
    Q_INVOKABLE bool runCommand(const QString &id);
    Q_INVOKABLE QVariantList commands() const;
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void copyLastAnswer();
    Q_INVOKABLE void copyLastQuestion();
    Q_INVOKABLE QString lastQuestion() const;

    // The hotkey client's context, applied before the window shows.
    void applyContext(const std::optional<Context> &context) { release(m_attachments.applyContext(context)); }
    Q_INVOKABLE void toggleAttachment(int row);
    Q_INVOKABLE void toggleScreen();
    Q_INVOKABLE void removeAttachment(int row);
    Q_INVOKABLE bool removeLastAttachment();
    Q_INVOKABLE bool pasteImage();
    Q_INVOKABLE int addImageFiles(const QList<QUrl> &urls);

    Q_INVOKABLE void loadModels();
    Q_INVOKABLE void setModel(const QString &id);

signals:
    void statusChanged();
    void canResumeChanged();
    void modelChanged();
    void modelsChanged();

private:
    void setStatus(Status status, const QString &error = {});
    bool ensureWorkspace();
    void finished(QProcess *process, int code, QProcess::ExitStatus exit, const QByteArray &errors);
    static QString failureText(int code, const QByteArray &stderrBytes);
    bool ensureRuntimeDir();
    QString newRuntimeFile(const QString &stem, const QString &suffix);
    void startCapture(int row);
    void release(const QList<Attachment> &attachments);
    void releaseInFlight();

    QString m_fxPath, m_stateDir, m_workspace, m_grim, m_runtimeDir, m_settingsFile;
    MessagesModel m_messages;
    AttachmentsModel m_attachments;
    QStringList m_inFlightFiles;
    int m_fileCounter = 0;
    Status m_status = Idle;
    QString m_errorText;
    bool m_canResume = false;
    QString m_model;
    QStringList m_models;
    ModelsState m_modelsState = ModelsIdle;
    QPointer<QProcess> m_process;
    QStringDecoder m_decoder{QStringConverter::Utf8};
};
