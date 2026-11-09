#include "instance.h"

#include <QLocalSocket>
#include <unistd.h>

Instance::Instance(const QString &name, QObject *parent)
    : QObject(parent), m_name(name.isEmpty() ? QString("ask-%1").arg(getuid()) : name) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                // Any other message (a "ping") only proves the instance is alive.
                if (socket->readAll().contains("toggle"))
                    emit toggleRequested();
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

bool Instance::notifyRunning(const QByteArray &message) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(300))
        return false;
    socket.write(message);
    return socket.waitForBytesWritten(300);
}

bool Instance::listen() {
    // Clears a socket file left behind by a crash; we only get here when no
    // live instance answered.
    QLocalServer::removeServer(m_name);
    return m_server.listen(m_name);
}
