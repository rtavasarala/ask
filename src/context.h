#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <optional>

// What the launching client saw at hotkey time, before Ask took focus.
struct Context {
    QString appClass, windowTitle, stableId, selection;

    static constexpr int kMaxSelection = 20000;
    static constexpr int kMaxTitle = 200;
    static constexpr int kMaxClass = 100;

    bool isEmpty() const { return stableId.isEmpty() && selection.isEmpty(); }

    // Parses and bounds one untrusted JSON object. Anything malformed (not an
    // object, a non-string field, a stableId that is not hex) yields nullopt.
    static std::optional<Context> fromJson(const QByteArray &json);
    QByteArray toJson() const;

    // Reads the active window from Hyprland and the primary selection from
    // wl-paste before Ask takes focus. Every step has a hard timeout and a
    // missing or non-hex window id only leaves `stableId` empty without
    // discarding `selection`. An empty command path means the system default.
    struct Sources {
        QString hyprSocket;  // default: $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock
        QString hyprctl = "hyprctl";
        QString wlPaste = "wl-paste";
        int selectionTimeoutMs = 150;
    };
    static Context gather(const Sources &sources);
    static Context gather() { return gather(Sources{}); }
};

// Signals carry it by value, and tests listen to them.
Q_DECLARE_METATYPE(std::optional<Context>)
