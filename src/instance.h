#pragma once

#include <QLocalServer>
#include <QObject>
#include <functional>

#include "context.h"

// Single resident instance. A second launch is a message to the first, so a
// hotkey costs one socket round trip and no platform plugin.
//
// Protocol, one JSON object per line over the local socket:
//   client -> {"cmd":"ping"}                     (autostart; never toggles)
//   client -> {"cmd":"toggle"}
//   server -> {"reply":"hidden"}                 the window was visible, now hiding
//   server -> {"reply":"need-context"}           the window is hidden and will show
//   client -> {"cmd":"context","context":{...}}  the client gathered it only now
// The window is shown only after the context (or a short timeout) arrives, so
// the user never sees stale chips and a hide never pays for gathering.
class Instance : public QObject {
    Q_OBJECT
public:
    explicit Instance(const QString &name = {}, QObject *parent = nullptr);

    // Answers "is the window visible right now?" for the toggle decision.
    void setVisibleProbe(std::function<bool()> probe) { m_visible = std::move(probe); }

    enum Outcome { NotRunning, Pinged, Hid, Showed };
    // Client side. `gather` runs only when the resident instance asks for it.
    Outcome toggleRunning(const std::function<Context()> &gather);
    bool pingRunning();
    bool listen();

signals:
    void hideRequested();
    // `context` is nullopt when the client sent none or sent something malformed.
    void showRequested(std::optional<Context> context);

private:
    void handle(QLocalSocket *socket);

    QString m_name;
    QLocalServer m_server;
    std::function<bool()> m_visible = [] { return false; };
};
