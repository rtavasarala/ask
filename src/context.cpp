#include "context.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>

namespace {
const QRegularExpression &hexIdPattern() {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{1,16}$"));
    return pattern;
}

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
    constexpr qsizetype maxBytes = Context::kMaxSelection * 4;
    QProcess process;
    process.start(sources.wlPaste, {"--primary", "--no-newline", "--type", "text"}, QIODevice::ReadOnly);
    QElapsedTimer timer;
    timer.start();
    if (!process.waitForStarted(sources.selectionTimeoutMs))
        return {};

    QByteArray raw;
    bool timedOut = false;
    while (raw.size() < maxBytes) {
        if (process.bytesAvailable() > 0) {
            raw += process.read(maxBytes - raw.size());
            continue;
        }
        if (process.state() == QProcess::NotRunning)
            break;
        const int remaining = sources.selectionTimeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            timedOut = true;
            break;
        }
        if (!process.waitForReadyRead(remaining)) {
            // Pipe EOF can arrive just before the child is reaped; let any remaining budget collect exitCode.
            const int left = std::max(0, sources.selectionTimeoutMs - static_cast<int>(timer.elapsed()));
            if (process.state() != QProcess::NotRunning && !process.waitForFinished(left))
                timedOut = true;
            if (raw.size() < maxBytes && process.bytesAvailable() > 0)
                raw += process.read(maxBytes - raw.size());
            break;
        }
        raw += process.read(maxBytes - raw.size());
    }

    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(50);
    }
    if (raw.size() < maxBytes) {
        if (timedOut || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return {};
    }

    // Stateful UTF-8 decoding omits a split trailing code point at maxBytes instead of appending U+FFFD.
    QStringDecoder decoder(QStringConverter::Utf8);
    return QString(decoder(raw)).left(Context::kMaxSelection);
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
    if (!context.stableId.isEmpty() && !hexIdPattern().match(context.stableId).hasMatch())
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

    const QJsonObject window = QJsonDocument::fromJson(activeWindowFromHyprland(sources).toUtf8()).object();
    const QString appClass = window.value("class").toString();
    if (appClass == "ask")
        return {};

    Context context;
    context.appClass = appClass.left(kMaxClass);
    context.windowTitle = window.value("title").toString().left(kMaxTitle);

    QString stableId = window.value("stableId").toString().trimmed().toLower();
    if (stableId.startsWith(QStringLiteral("0x")))
        stableId.remove(0, 2);
    if (hexIdPattern().match(stableId).hasMatch())
        context.stableId = stableId;

    context.selection = primarySelection(sources).left(kMaxSelection);
    return context;
}
