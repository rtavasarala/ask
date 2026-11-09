#include "instance.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <memory>
#include <unistd.h>

namespace {
constexpr int kContextWaitMs = 500;
constexpr qsizetype kMaxLine = 256 * 1024;

QByteArray line(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n"; }

bool readLine(QLocalSocket &socket, QByteArray *out, int timeoutMs) {
    while (!out->contains('\n')) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
        *out += socket.readAll();
    }
    return true;
}
}

Instance::Instance(const QString &name, QObject *parent)
    : QObject(parent), m_name(!name.isEmpty() ? name
                              : qEnvironmentVariableIsSet("ASK_INSTANCE") ? qEnvironmentVariable("ASK_INSTANCE")
                                                                          : QString("ask-%1").arg(getuid())) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            handle(socket);
        }
    });
}

void Instance::handle(QLocalSocket *socket) {
    auto buffer = std::make_shared<QByteArray>();
    auto awaitingContext = std::make_shared<bool>(false);

    connect(socket, &QLocalSocket::disconnected, this, [this, awaitingContext] {
        if (*awaitingContext) {
            *awaitingContext = false;
            emit showRequested(std::nullopt);
        }
    });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, awaitingContext] {
        *buffer += socket->readAll();
        if (buffer->size() > kMaxLine) {
            socket->disconnectFromServer();
            return;
        }
        qsizetype end;
        while ((end = buffer->indexOf('\n')) >= 0) {
            const QByteArray message = buffer->left(end);
            buffer->remove(0, end + 1);
            const QJsonObject object = QJsonDocument::fromJson(message).object();
            const QString cmd = object.value("cmd").toString();
            if (cmd == "toggle" && !*awaitingContext) {
                if (m_visible()) {
                    socket->write(line({{"reply", "hidden"}}));
                    socket->disconnectFromServer();
                    emit hideRequested();
                    return;
                }
                *awaitingContext = true;
                socket->write(line({{"reply", "need-context"}}));
                // A client that dies or stalls must not leave the hotkey dead.
                QTimer::singleShot(kContextWaitMs, socket, [this, socket, awaitingContext] {
                    if (*awaitingContext) {
                        *awaitingContext = false;
                        emit showRequested(std::nullopt);
                        socket->disconnectFromServer();
                    }
                });
            } else if (cmd == "context" && *awaitingContext) {
                *awaitingContext = false;
                const QJsonValue value = object.value("context");
                std::optional<Context> context;
                if (value.isObject())
                    context = Context::fromJson(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
                emit showRequested(context);
                socket->disconnectFromServer();
                return;
            } else {
                // "ping" or anything unknown only proves the instance is alive.
                socket->disconnectFromServer();
                return;
            }
        }
    });
}

bool Instance::pingRunning() {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(300))
        return false;
    socket.write(line({{"cmd", "ping"}}));
    return socket.waitForBytesWritten(300);
}

Instance::Outcome Instance::toggleRunning(const std::function<Context()> &gather) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(300))
        return NotRunning;
    socket.write(line({{"cmd", "toggle"}}));
    QByteArray reply;
    if (!socket.waitForBytesWritten(300) || !readLine(socket, &reply, 1000))
        return Pinged;
    const QString answer = QJsonDocument::fromJson(reply).object().value("reply").toString();
    if (answer == "hidden")
        return Hid;
    if (answer != "need-context")
        return Pinged;
    const Context context = gather();
    socket.write(line({{"cmd", "context"}, {"context", QJsonDocument::fromJson(context.toJson()).object()}}));
    socket.waitForBytesWritten(300);
    return Showed;
}

bool Instance::listen() {
    // Clears a socket file left behind by a crash; we only get here when no
    // live instance answered.
    QLocalServer::removeServer(m_name);
    return m_server.listen(m_name);
}
