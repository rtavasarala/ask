#include "context.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>

namespace {
bool readString(const QJsonObject &object, const char *key, QString *out, int limit) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isString())
        return false;
    *out = value.toString().left(limit);
    return true;
}

QString activeWindowFromHyprland(const Context::Sources &sources) {
    QString path = sources.hyprSocket;
    if (path.isEmpty()) {
        const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR"), signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!runtime.isEmpty() && !signature.isEmpty())
            path = QString::fromLocal8Bit(runtime) + "/hypr/" + QString::fromLocal8Bit(signature) + "/.socket.sock";
    }
    if (!path.isEmpty()) {
        QLocalSocket socket;
        socket.connectToServer(path);
        if (socket.waitForConnected(100)) {
            socket.write("j/activewindow");
            socket.waitForBytesWritten(100);
            QByteArray reply;
            // Hyprland closes the connection after answering.
            while (socket.state() != QLocalSocket::UnconnectedState && socket.waitForReadyRead(100))
                reply += socket.readAll();
            reply += socket.readAll();
            if (!reply.isEmpty())
                return QString::fromUtf8(reply);
        }
    }
    QProcess process;
    process.start(sources.hyprctl, {"activewindow", "-j"});
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return QString::fromUtf8(process.readAllStandardOutput());
    return {};
}

QString primarySelection(const Context::Sources &sources) {
    QProcess process;
    process.start(sources.wlPaste, {"--primary", "--no-newline", "--type", "text"}, QIODevice::ReadOnly);
    if (!process.waitForFinished(sources.selectionTimeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return {};
    }
    if (process.exitCode() != 0)
        return {};
    // Bytes, not characters: a hostile selection must not cost more than this.
    return QString::fromUtf8(process.readAllStandardOutput().left(Context::kMaxSelection * 4));
}
}

std::optional<Context> Context::fromJson(const QByteArray &json) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    Context context;
    if (!readString(object, "appClass", &context.appClass, kMaxClass) ||
        !readString(object, "windowTitle", &context.windowTitle, kMaxTitle) ||
        !readString(object, "stableId", &context.stableId, 64) ||
        !readString(object, "selection", &context.selection, kMaxSelection))
        return std::nullopt;
    static const QRegularExpression hex("^[0-9a-f]{1,16}$");
    if (!context.stableId.isEmpty() && !hex.match(context.stableId).hasMatch())
        return std::nullopt;
    return context;
}

QByteArray Context::toJson() const {
    return QJsonDocument(QJsonObject{{"appClass", appClass}, {"windowTitle", windowTitle},
                                     {"stableId", stableId}, {"selection", selection}})
        .toJson(QJsonDocument::Compact);
}

Context Context::gather(const Sources &sources) {
    // QProcess needs an application object even when it never runs an event loop.
    std::optional<QCoreApplication> owned;
    int argc = 1;
    char name[] = "ask";
    char *argv[] = {name, nullptr};
    if (!QCoreApplication::instance())
        owned.emplace(argc, argv);

    Context context;
    const QJsonObject window = QJsonDocument::fromJson(activeWindowFromHyprland(sources).toUtf8()).object();
    const QString appClass = window.value("class").toString();
    // Ask itself, or nothing focused: there is no context to offer.
    if (!appClass.isEmpty() && appClass != "ask") {
        context.appClass = appClass.left(kMaxClass);
        context.windowTitle = window.value("title").toString().left(kMaxTitle);
        context.stableId = window.value("stableId").toString();
        context.selection = primarySelection(sources).left(kMaxSelection);
        // Normalize through the same boundary the server applies.
        if (const auto checked = fromJson(context.toJson()))
            return *checked;
        return {};
    }
    return {};
}
