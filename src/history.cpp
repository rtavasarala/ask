#include "history.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

#include "attachment.h"

namespace {
bool isValidChatId(const QString &id) {
    return !id.isEmpty() && id != QLatin1String(".") && id != QLatin1String("..")
           && !id.contains('/') && !id.contains('\\');
}

// Ensures we only ever delete direct, non-symlink subdirectories of stateDir/chats
// and never touch the active workspace.
bool isSafeRemovableChatDir(const QString &chatsRoot, const QString &candidate, const QString &activeWorkspace) {
    if (chatsRoot.isEmpty() || candidate.isEmpty())
        return false;
    const QString root = QDir::cleanPath(QFileInfo(chatsRoot).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
    const QString active = activeWorkspace.isEmpty()
        ? QString()
        : QDir::cleanPath(QFileInfo(activeWorkspace).absoluteFilePath());
    if (root.isEmpty() || root == QLatin1String("/") || target == active)
        return false;
    if (!target.startsWith(root + QLatin1Char('/')))
        return false;
    const QString rel = target.mid(root.size() + 1);
    if (!isValidChatId(rel))
        return false;
    const QFileInfo info(target);
    return info.exists() && info.isDir() && !info.isSymLink();
}

QString firstUserTitle(const QList<MessagesModel::Turn> &turns) {
    for (const MessagesModel::Turn &turn : turns) {
        if (turn.role == QLatin1String("user") && !turn.text.trimmed().isEmpty())
            return oneLinePreview(turn.text);
    }
    return {};
}

void sortChatsNewestFirst(QList<SavedChat> &chats) {
    std::stable_sort(chats.begin(), chats.end(), [](const SavedChat &a, const SavedChat &b) {
        return a.updatedAt > b.updatedAt;
    });
}
}

QString History::chatsDir(const QString &stateDir) {
    return QDir::cleanPath(stateDir) + QStringLiteral("/chats");
}

QString History::indexPath(const QString &stateDir) {
    return chatsDir(stateDir) + QStringLiteral("/index.json");
}

QList<SavedChat> History::load(const QString &stateDir) {
    QList<SavedChat> chats;
    QFile file(indexPath(stateDir));
    if (!file.open(QIODevice::ReadOnly))
        return chats;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return chats;

    const QJsonArray array = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("chats")).toArray();
    QSet<QString> seen;
    for (const QJsonValue &val : array) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString().trimmed();
        if (!isValidChatId(id) || seen.contains(id))
            continue;
        seen.insert(id);

        SavedChat chat;
        chat.id = id;
        chat.workspace = obj.value(QStringLiteral("workspace")).toString().trimmed();
        if (chat.workspace.isEmpty())
            chat.workspace = chatsDir(stateDir) + QLatin1Char('/') + id;
        chat.model = obj.value(QStringLiteral("model")).toString().trimmed();
        chat.updatedAt = obj.value(QStringLiteral("updatedAt")).toInteger(0);
        for (const QJsonValue &tv : obj.value(QStringLiteral("turns")).toArray()) {
            if (!tv.isObject())
                continue;
            const QJsonObject to = tv.toObject();
            MessagesModel::Turn turn;
            turn.role = to.value(QStringLiteral("role")).toString();
            turn.text = to.value(QStringLiteral("text")).toString();
            turn.state = to.value(QStringLiteral("state")).toString();
            turn.note = to.value(QStringLiteral("note")).toString();
            if (!turn.role.isEmpty())
                chat.turns << turn;
        }
        chat.title = obj.value(QStringLiteral("title")).toString().trimmed();
        if (chat.title.isEmpty())
            chat.title = firstUserTitle(chat.turns);
        chats << chat;
    }
    sortChatsNewestFirst(chats);
    return chats;
}

bool History::save(const QString &stateDir, const QList<SavedChat> &chats) {
    const QString path = indexPath(stateDir);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QList<SavedChat> ordered = chats;
    sortChatsNewestFirst(ordered);
    if (ordered.size() > kMaxChats)
        ordered = ordered.mid(0, kMaxChats);

    QJsonArray array;
    for (const SavedChat &chat : ordered) {
        if (!isValidChatId(chat.id))
            continue;
        QJsonArray turnsJson;
        for (const MessagesModel::Turn &turn : chat.turns) {
            const QString state = turn.state == QLatin1String("streaming") ? QStringLiteral("done") : turn.state;
            turnsJson.append(QJsonObject{
                {QStringLiteral("role"), turn.role},
                {QStringLiteral("text"), turn.text},
                {QStringLiteral("state"), state},
                {QStringLiteral("note"), turn.note},
            });
        }
        array.append(QJsonObject{
            {QStringLiteral("id"), chat.id},
            {QStringLiteral("title"), chat.title.isEmpty() ? firstUserTitle(chat.turns) : chat.title},
            {QStringLiteral("workspace"), chat.workspace},
            {QStringLiteral("model"), chat.model},
            {QStringLiteral("updatedAt"), chat.updatedAt},
            {QStringLiteral("turns"), turnsJson},
        });
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("chats"), array},
    };
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool History::upsert(const QString &stateDir, SavedChat chat) {
    if (chat.workspace.isEmpty() && isValidChatId(chat.id))
        chat.workspace = chatsDir(stateDir) + QLatin1Char('/') + chat.id;
    if (chat.id.isEmpty() && !chat.workspace.isEmpty())
        chat.id = QFileInfo(QDir::cleanPath(chat.workspace)).fileName();
    if (!isValidChatId(chat.id))
        return false;
    if (chat.title.isEmpty())
        chat.title = firstUserTitle(chat.turns);
    if (chat.updatedAt <= 0)
        chat.updatedAt = QDateTime::currentMSecsSinceEpoch();

    QList<SavedChat> chats = load(stateDir);
    const QString cleanWorkspace = QDir::cleanPath(chat.workspace);
    for (int i = chats.size() - 1; i >= 0; --i) {
        if (chats.at(i).id == chat.id || QDir::cleanPath(chats.at(i).workspace) == cleanWorkspace)
            chats.removeAt(i);
    }
    chats.prepend(chat);
    sortChatsNewestFirst(chats);

    const QString root = chatsDir(stateDir);
    while (chats.size() > kMaxChats) {
        const SavedChat overflow = chats.takeLast();
        if (isSafeRemovableChatDir(root, overflow.workspace, chat.workspace))
            QDir(overflow.workspace).removeRecursively();
    }

    const bool ok = save(stateDir, chats);
    prune(stateDir, chat.workspace);
    return ok;
}

bool History::recordChat(const QString &stateDir, const QString &workspace, const QString &model,
                         const QList<MessagesModel::Turn> &turns, qint64 updatedAt) {
    if (workspace.isEmpty() || turns.isEmpty())
        return false;
    SavedChat chat;
    chat.id = QFileInfo(QDir::cleanPath(workspace)).fileName();
    chat.title = firstUserTitle(turns);
    chat.workspace = workspace;
    chat.model = model;
    chat.updatedAt = updatedAt > 0 ? updatedAt : QDateTime::currentMSecsSinceEpoch();
    chat.turns = turns;
    return upsert(stateDir, std::move(chat));
}

std::optional<SavedChat> History::find(const QString &stateDir, const QString &id) {
    if (!isValidChatId(id))
        return std::nullopt;
    for (const SavedChat &chat : load(stateDir)) {
        if (chat.id == id)
            return chat;
    }
    return std::nullopt;
}

bool History::remove(const QString &stateDir, const QString &id) {
    if (!isValidChatId(id))
        return false;
    QList<SavedChat> chats = load(stateDir);
    bool removed = false;
    const QString root = chatsDir(stateDir);
    for (int i = chats.size() - 1; i >= 0; --i) {
        if (chats.at(i).id != id)
            continue;
        const SavedChat target = chats.takeAt(i);
        if (isSafeRemovableChatDir(root, target.workspace, {}))
            QDir(target.workspace).removeRecursively();
        removed = true;
    }
    if (!removed)
        return false;
    return save(stateDir, chats);
}

void History::prune(const QString &stateDir, const QString &activeWorkspace) {
    const QString root = chatsDir(stateDir);
    const bool rootExists = QDir(root).exists();
    const bool indexExists = QFileInfo::exists(indexPath(stateDir));
    if (!rootExists && !indexExists)
        return;

    QList<SavedChat> chats = load(stateDir);
    bool modified = false;

    for (int i = chats.size() - 1; i >= 0; --i) {
        if (!QFileInfo(chats.at(i).workspace).isDir()) {
            chats.removeAt(i);
            modified = true;
        }
    }

    while (chats.size() > kMaxChats) {
        const SavedChat overflow = chats.takeLast();
        if (isSafeRemovableChatDir(root, overflow.workspace, activeWorkspace))
            QDir(overflow.workspace).removeRecursively();
        modified = true;
    }

    if (modified && (indexExists || !chats.isEmpty()))
        save(stateDir, chats);

    if (!rootExists)
        return;

    QSet<QString> kept;
    for (const SavedChat &chat : chats)
        kept.insert(QDir::cleanPath(QFileInfo(chat.workspace).absoluteFilePath()));
    if (!activeWorkspace.isEmpty())
        kept.insert(QDir::cleanPath(QFileInfo(activeWorkspace).absoluteFilePath()));

    for (const QFileInfo &entry : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dirPath = QDir::cleanPath(entry.absoluteFilePath());
        if (!kept.contains(dirPath) && isSafeRemovableChatDir(root, dirPath, activeWorkspace))
            QDir(dirPath).removeRecursively();
    }
}
