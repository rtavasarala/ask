#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <utility>

// ~/.config/ask/settings.json. Unknown or malformed contents fall back to defaults.
struct Settings {
    QString model;   // empty means fx's default
    QString effort;  // empty means fx's default for the model
    bool fast = false;

    Settings(QString model = {}, QString effort = {}, bool fast = false)
        : model(std::move(model)), effort(std::move(effort)), fast(fast) {}

    static Settings load(const QString &path);
    // Writes beside the target and renames, so a crash never leaves half a file.
    bool save(const QString &path) const;
    static QString defaultPath();
};

struct ModelInfo {
    QString id;
    QString name;
    QStringList efforts;
    bool fast = false;
    bool ultrafast = false;
};

// Parses `fx models --json` output, falling back to plain-text `- <id>` lines.
QList<ModelInfo> parseModelInfos(const QByteArray &output);
QStringList parseModels(const QByteArray &output);
// "anthropic/claude-sonnet-5.5" -> "claude-sonnet-5.5".
QString shortModelName(const QString &id);
// Resolves `fx` from PATH, then ~/.local/bin/fx where fx installs itself.
QString resolveFxExecutable();
