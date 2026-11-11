#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "messagesmodel.h"

struct SavedChat {
    QString id;          // directory basename, e.g. "20261111-120000-123"
    QString title;       // oneLinePreview of first user prompt
    QString workspace;   // full path to workspace dir
    QString model;
    qint64 updatedAt = 0;
    QList<MessagesModel::Turn> turns;
};

// Persists recent chat metadata in stateDir/chats/index.json and prunes old or
// orphaned workspace directories so stateDir stays bounded.
class History {
public:
    static constexpr int kMaxChats = 30;

    static QString chatsDir(const QString &stateDir);
    static QString indexPath(const QString &stateDir);

    static QList<SavedChat> load(const QString &stateDir);
    static bool save(const QString &stateDir, const QList<SavedChat> &chats);

    static bool upsert(const QString &stateDir, SavedChat chat);
    static bool recordChat(const QString &stateDir, const QString &workspace, const QString &model,
                           const QList<MessagesModel::Turn> &turns, qint64 updatedAt = 0);

    static std::optional<SavedChat> find(const QString &stateDir, const QString &id);
    static bool remove(const QString &stateDir, const QString &id);
    static void prune(const QString &stateDir, const QString &activeWorkspace = {});
};
