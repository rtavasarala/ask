#include "context.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStringDecoder>
#include <QStringView>
#include <algorithm>

namespace {
bool isLowerHexId(QStringView s) {
    if (s.isEmpty() || s.size() > 16)
        return false;
    for (QChar c : s) {
        const char16_t u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f')))
            return false;
    }
    return true;
}

bool readString(const QJsonObject &object, const char *key, QString *out, int limit) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isString())
        return false;
    *out = value.toString().left(limit);
    return true;
}

QByteArray activeWindowFromHyprland(const Context::Sources &sources) {
    QString path = sources.hyprSocket;
    if (path.isEmpty()) {
        const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR"), signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!runtime.isEmpty() && !signature.isEmpty())
            path = QString::fromLocal8Bit(runtime) + QStringLiteral("/hypr/")
                   + QString::fromLocal8Bit(signature) + QStringLiteral("/.socket.sock");
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
                return reply;
        }
    }
    QProcess process;
    process.start(sources.hyprctl, {QStringLiteral("activewindow"), QStringLiteral("-j")}, QIODevice::ReadOnly);
    if (process.waitForFinished(500) && process.exitCode() == 0)
        return process.readAllStandardOutput();
    return {};
}

QString collectPrimarySelection(QProcess &process, const QElapsedTimer &timer, int timeoutMs) {
    constexpr qsizetype maxBytes = Context::kMaxSelection * 4;
    const int startBudget = std::max(0, timeoutMs - static_cast<int>(timer.elapsed()));
    if (process.state() == QProcess::Starting && !process.waitForStarted(startBudget)) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(50);
        }
        return {};
    }

    QByteArray raw;
    bool timedOut = false;
    bool probedZero = false;
    while (raw.size() < maxBytes) {
        if (process.bytesAvailable() > 0) {
            raw += process.read(maxBytes - raw.size());
            continue;
        }
        if (process.state() == QProcess::NotRunning)
            break;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0 && probedZero) {
            timedOut = true;
            break;
        }
        if (remaining <= 0)
            probedZero = true;
        if (!process.waitForReadyRead(std::max(0, remaining))) {
            // Pipe EOF can arrive just before the child is reaped; let any remaining budget collect exitCode.
            const int left = std::max(0, timeoutMs - static_cast<int>(timer.elapsed()));
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
}  // namespace

std::optional<Context> Context::fromJsonObject(const QJsonObject &object) {
    Context context;
    if (!readString(object, "appClass", &context.appClass, kMaxClass) ||
        !readString(object, "windowTitle", &context.windowTitle, kMaxTitle) ||
        !readString(object, "stableId", &context.stableId, 64) ||
        !readString(object, "selection", &context.selection, kMaxSelection))
        return std::nullopt;
    if (!context.stableId.isEmpty() && !isLowerHexId(context.stableId))
        return std::nullopt;
    return context;
}

std::optional<Context> Context::fromJson(const QByteArray &json) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return fromJsonObject(document.object());
}

QJsonObject Context::toJsonObject() const {
    return QJsonObject{
        {QStringLiteral("appClass"), appClass},
        {QStringLiteral("windowTitle"), windowTitle},
        {QStringLiteral("stableId"), stableId},
        {QStringLiteral("selection"), selection},
    };
}

QByteArray Context::toJson() const {
    return QJsonDocument(toJsonObject()).toJson(QJsonDocument::Compact);
}

Context Context::gather(const Sources &sources) {
    // QProcess needs an application object even when it never runs an event loop.
    std::optional<QCoreApplication> owned;
    int argc = 1;
    char name[] = "ask";
    char *argv[] = {name, nullptr};
    if (!QCoreApplication::instance())
        owned.emplace(argc, argv);

    // Start wl-paste concurrently so it overlaps with Hyprland IPC / hyprctl.
    QProcess wlPasteProc;
    QElapsedTimer selectionTimer;
    selectionTimer.start();
    wlPasteProc.start(sources.wlPaste,
                      {QStringLiteral("--primary"), QStringLiteral("--no-newline"),
                       QStringLiteral("--type"), QStringLiteral("text")},
                      QIODevice::ReadOnly);

    const QJsonObject window = QJsonDocument::fromJson(activeWindowFromHyprland(sources)).object();
    const QString appClass = window.value(QLatin1String("class")).toString();
    if (appClass == QLatin1String("ask")) {
        if (wlPasteProc.state() != QProcess::NotRunning) {
            wlPasteProc.kill();
            wlPasteProc.waitForFinished(50);
        }
        return {};
    }

    Context context;
    context.appClass = appClass.left(kMaxClass);
    context.windowTitle = window.value(QLatin1String("title")).toString().left(kMaxTitle);

    QString stableId = window.value(QLatin1String("stableId")).toString().trimmed().toLower();
    if (stableId.startsWith(QLatin1String("0x")))
        stableId.remove(0, 2);
    if (isLowerHexId(stableId))
        context.stableId = stableId;

    context.selection = collectPrimarySelection(wlPasteProc, selectionTimer, sources.selectionTimeoutMs).left(kMaxSelection);
    return context;
}
