#include "instance.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <unistd.h>
#include <utility>

namespace {
constexpr int kContextWaitMs = 500;
constexpr qsizetype kMaxLine = 256 * 1024;
constexpr int kMaxPrompt = 20000;
constexpr int kMaxCommand = 64;
constexpr int kMaxModel = 128;
constexpr int kMaxEffort = 32;
constexpr int kMaxFiles = 16;
constexpr int kMaxFilePath = 4096;

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
    qRegisterMetaType<std::optional<Context>>();
    qRegisterMetaType<Instance::Request>();

    // A client that dies or stalls must not leave the hotkey dead.
    m_contextTimer.setSingleShot(true);
    m_contextTimer.setInterval(kContextWaitMs);
    connect(&m_contextTimer, &QTimer::timeout, this, [this] {
        if (m_awaitingContext)
            finishPending(std::nullopt);
    });

    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            handle(socket);
        }
    });
}

Instance::~Instance() {
    m_awaitingContext = false;
    m_contextTimer.stop();
    m_pendingSocket.clear();
    m_pendingRequest.reset();
}

void Instance::cancelPending(QLocalSocket *except) {
    m_awaitingContext = false;
    m_contextTimer.stop();
    m_pendingRequest.reset();
    if (auto *pending = m_pendingSocket.data()) {
        m_pendingSocket.clear();
        if (pending != except)
            pending->disconnectFromServer();
    }
}

void Instance::finishPending(std::optional<Context> context) {
    m_awaitingContext = false;
    m_contextTimer.stop();
    const auto request = std::exchange(m_pendingRequest, std::nullopt);
    auto *socket = m_pendingSocket.data();
    m_pendingSocket.clear();
    if (socket && socket->state() != QLocalSocket::UnconnectedState)
        socket->disconnectFromServer();
    if (request)
        emit requestReceived(std::move(context), *request);
    else
        emit showRequested(std::move(context));
}

void Instance::handle(QLocalSocket *socket) {
    auto buffer = std::make_shared<QByteArray>();

    connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
        if (m_awaitingContext && m_pendingSocket == socket)
            finishPending(std::nullopt);
    });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
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
            if (cmd == "toggle") {
                // Rapid double-press before context arrives cancels the pending show.
                if (m_awaitingContext) {
                    cancelPending(socket);
                    socket->write(line({{"reply", "hidden"}}));
                    socket->disconnectFromServer();
                    emit hideRequested();
                    return;
                }
                if (m_visible()) {
                    socket->write(line({{"reply", "hidden"}}));
                    socket->disconnectFromServer();
                    emit hideRequested();
                    return;
                }
                if (m_gather) {
                    const Context ctx = m_gather();
                    socket->write(line({{"reply", "shown"}}));
                    socket->disconnectFromServer();
                    emit showRequested(ctx);
                    return;
                }
                m_awaitingContext = true;
                m_pendingSocket = socket;
                m_pendingRequest.reset();
                socket->write(line({{"reply", "need-context"}}));
                m_contextTimer.start(kContextWaitMs);
            } else if (cmd == "open") {
                if (m_awaitingContext)
                    cancelPending(socket);
                Request request;
                request.prompt = object.value("prompt").toString().left(kMaxPrompt);
                request.commandId = object.value("command").toString().left(kMaxCommand);
                request.newChat = object.value("newChat").toBool(false);
                request.model = object.value("model").toString().left(kMaxModel);
                request.effort = object.value("effort").toString().left(kMaxEffort);
                request.fast = object.value("fast").toBool(false);
                const QJsonArray files = object.value("files").toArray();
                const qsizetype fileCount = qMin<qsizetype>(files.size(), kMaxFiles);
                for (qsizetype i = 0; i < fileCount; ++i) {
                    const QString path = files.at(i).toString().left(kMaxFilePath);
                    if (!path.isEmpty())
                        request.files << path;
                }
                if (m_visible()) {
                    socket->write(line({{"reply", "shown"}}));
                    socket->disconnectFromServer();
                    emit requestReceived(std::nullopt, request);
                    return;
                }
                if (m_gather) {
                    const Context ctx = m_gather();
                    socket->write(line({{"reply", "shown"}}));
                    socket->disconnectFromServer();
                    emit requestReceived(ctx, request);
                    return;
                }
                m_awaitingContext = true;
                m_pendingSocket = socket;
                m_pendingRequest = request;
                socket->write(line({{"reply", "need-context"}}));
                m_contextTimer.start(kContextWaitMs);
            } else if (cmd == "context" && m_awaitingContext && m_pendingSocket == socket) {
                const QJsonValue value = object.value("context");
                std::optional<Context> context;
                if (value.isObject())
                    context = Context::fromJson(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
                finishPending(std::move(context));
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
    if (!socket.waitForBytesWritten(300) || !readLine(socket, &reply, 1500))
        return Pinged;
    const QString answer = QJsonDocument::fromJson(reply).object().value("reply").toString();
    if (answer == "hidden")
        return Hid;
    if (answer == "shown")
        return Showed;
    if (answer != "need-context")
        return Pinged;
    const Context context = gather ? gather() : Context{};
    socket.write(line({{"cmd", "context"}, {"context", QJsonDocument::fromJson(context.toJson()).object()}}));
    socket.waitForBytesWritten(300);
    return Showed;
}

Instance::Outcome Instance::sendRequest(const Request &request, const std::function<Context()> &gather) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(300))
        return NotRunning;
    socket.write(line({{"cmd", "open"},
                       {"prompt", request.prompt},
                       {"command", request.commandId},
                       {"newChat", request.newChat},
                       {"files", QJsonArray::fromStringList(request.files)},
                       {"model", request.model},
                       {"effort", request.effort},
                       {"fast", request.fast}}));
    QByteArray reply;
    if (!socket.waitForBytesWritten(300) || !readLine(socket, &reply, 1500))
        return Pinged;
    const QString answer = QJsonDocument::fromJson(reply).object().value("reply").toString();
    if (answer == "shown")
        return Showed;
    if (answer == "hidden")
        return Hid;
    if (answer != "need-context")
        return Pinged;
    const Context context = gather ? gather() : Context{};
    socket.write(line({{"cmd", "context"}, {"context", QJsonDocument::fromJson(context.toJson()).object()}}));
    socket.waitForBytesWritten(300);
    return Showed;
}

bool Instance::listen() {
    // Clears a socket file left behind by a crash; we only get here when no
    // live instance answered.
    QLocalServer::removeServer(m_name);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    return m_server.listen(m_name);
}
