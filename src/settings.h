#pragma once

#include <QString>
#include <QStringList>

// ~/.config/ask/settings.json. Unknown or malformed contents fall back to defaults.
struct Settings {
    QString model;  // empty means fx's default

    static Settings load(const QString &path);
    // Writes beside the target and renames, so a crash never leaves half a file.
    bool save(const QString &path) const;
    static QString defaultPath();
};

// `fx models` prints "[models] N available" then one " - <id>" line per model.
QStringList parseModels(const QByteArray &output);
// "anthropic/claude-sonnet-5.5" -> "claude-sonnet-5.5".
QString shortModelName(const QString &id);
