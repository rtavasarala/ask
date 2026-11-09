#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringDecoder>

#include "messagesmodel.h"

// One chat with `fx ask`. Each chat owns a fresh workspace directory because
// fx scopes `--resume last` to the working directory.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(MessagesModel *messages READ messages CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY canResumeChanged)

public:
    enum Status { Idle, Running, Failed, Stopped };
    Q_ENUM(Status)

    // An empty fxPath is resolved from PATH, then ~/.local/bin; an empty
    // stateDir means ~/.local/state/ask.
    explicit Backend(const QString &fxPath = {}, const QString &stateDir = {}, QObject *parent = nullptr);
    ~Backend() override;

    MessagesModel *messages() { return &m_messages; }
    Status status() const { return m_status; }
    QString errorText() const { return m_errorText; }
    bool canResume() const { return m_canResume; }
    QString workspace() const { return m_workspace; }

    Q_INVOKABLE void send(const QString &prompt);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void copyLastAnswer();
    Q_INVOKABLE void copyLastQuestion();
    Q_INVOKABLE QString lastQuestion() const;

signals:
    void statusChanged();
    void canResumeChanged();

private:
    void setStatus(Status status, const QString &error = {});
    bool ensureWorkspace();
    void finished(QProcess *process, int code, QProcess::ExitStatus exit, const QByteArray &errors);
    static QString failureText(int code, const QByteArray &stderrBytes);

    QString m_fxPath, m_stateDir, m_workspace;
    MessagesModel m_messages;
    Status m_status = Idle;
    QString m_errorText;
    bool m_canResume = false;
    QPointer<QProcess> m_process;
    QStringDecoder m_decoder{QStringConverter::Utf8};
};
