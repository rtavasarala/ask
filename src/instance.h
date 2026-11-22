#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <optional>

#include "context.h"

// Single resident instance. A second launch is a message to the first, so a
// hotkey costs one socket round trip and no platform plugin.
//
// Protocol, one JSON object per line over the local socket:
//   client -> {"cmd":"ping"}                     (autostart; never toggles)
//   client -> {"cmd":"toggle"}
//   client -> {"cmd":"open","prompt":"...","command":"...","newChat":false}
//   server -> {"reply":"hidden"}                 the window was visible, now hiding
//   server -> {"reply":"shown"}                  the server gathered context itself
//   server -> {"reply":"need-context"}           the window is hidden and awaits client context
//   client -> {"cmd":"context","context":{...}}  the client gathered it only now
// The window is shown only after the context (or a short timeout) arrives, so
// the user never sees stale chips and a hide never pays for gathering.
class Instance : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString prompt;
        QString commandId;
        bool newChat = false;
        QStringList files;
        QString model;
        QString effort;
        bool fast = false;

        bool operator==(const Request &other) const {
            return prompt == other.prompt && commandId == other.commandId && newChat == other.newChat
                   && files == other.files && model == other.model && effort == other.effort && fast == other.fast;
        }
    };

    explicit Instance(const QString &name = {}, QObject *parent = nullptr);
    ~Instance() override;

    // Answers "is the window visible right now?" for the toggle decision.
    void setVisibleProbe(std::function<bool()> probe) { m_visible = std::move(probe); }
    // Optional server-side gatherer so toggles complete in one round trip.
    void setContextGatherer(std::function<Context()> gather) { m_gather = std::move(gather); }

    enum Outcome { NotRunning, Pinged, Hid, Showed };
    // Client side. `gather` runs only when the resident instance asks for it.
    Outcome toggleRunning(const std::function<Context()> &gather);
    Outcome sendRequest(const Request &request, const std::function<Context()> &gather);
    bool pingRunning();
    bool listen();

signals:
    void hideRequested();
    // `context` is nullopt when the client sent none or sent something malformed.
    void showRequested(std::optional<Context> context);
    void requestReceived(std::optional<Context> context, Instance::Request request);

private:
    void handle(QLocalSocket *socket);
    void cancelPending(QLocalSocket *except = nullptr);
    void finishPending(std::optional<Context> context);

    QString m_name;
    std::function<bool()> m_visible = [] { return false; };
    std::function<Context()> m_gather;
    QPointer<QLocalSocket> m_pendingSocket;
    std::optional<Request> m_pendingRequest;
    bool m_awaitingContext = false;
    QTimer m_contextTimer;
    QLocalServer m_server;
};

Q_DECLARE_METATYPE(Instance::Request)
