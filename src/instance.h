#pragma once

#include <QLocalServer>
#include <QObject>

// Single resident instance. A second launch sends "toggle" to the first and
// exits, so a hotkey is a message to a warm process.
class Instance : public QObject {
    Q_OBJECT
public:
    explicit Instance(const QString &name = {}, QObject *parent = nullptr);

    // True if a running instance accepted the message.
    bool notifyRunning(const QByteArray &message = "toggle\n");
    bool listen();

signals:
    void toggleRequested();

private:
    QString m_name;
    QLocalServer m_server;
};
