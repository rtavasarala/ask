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

QString cleanAbsPath(const QString &path) {
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Ensures we only ever delete direct, non-symlink subdirectories of stateDir/chats
// and never touch the active workspace.
bool isSafeRemovableChatDirClean(const QString &cleanRoot, const QString &cleanTarget, const QString &cleanActive) {
    if (cleanRoot.isEmpty() || cleanTarget.isEmpty() || cleanRoot == QLatin1String("/") || cleanTarget == cleanActive)
        return false;
    if (cleanTarget.size() <= cleanRoot.size() + 1 || !cleanTarget.startsWith(cleanRoot)
        || cleanTarget.at(cleanRoot.size()) != QLatin1Char('/'))
        return false;
    const QString rel = cleanTarget.mid(cleanRoot.size() + 1);
    if (!isValidChatId(rel))
        return false;
    const QFileInfo info(cleanTarget);
    return info.exists() && info.isDir() && !info.isSymLink();
}

QString firstUserTitle(const QList<MessagesModel::Turn> &turns) {
    for (const MessagesModel::Turn &turn : turns) {
        if (turn.role == QLatin1String("user") && !turn.text.trimmed().isEmpty())
            return oneLinePreview(turn.text);
    }
    return {};
}

QString firstUserTitleFromJson(const QJsonArray &turns) {
    for (const QJsonValue &tv : turns) {
        if (!tv.isObject())
            continue;
        const QJsonObject to = tv.toObject();
        if (to.value(QLatin1String("role")).toString() == QLatin1String("user")) {
            const QString text = to.value(QLatin1String("text")).toString();
            if (!text.trimmed().isEmpty())
                return oneLinePreview(text);
        }
    }
    return {};
}

template <typename ChatList>
void sortChatsNewestFirst(ChatList &chats) {
    std::stable_sort(chats.begin(), chats.end(), [](const auto &a, const auto &b) {
        return a.updatedAt > b.updatedAt;
    });
}

void removeOrphanDirs(const QString &root, const QList<SavedChat> &chats, const QString &activeWorkspace) {
    if (!QDir(root).exists())
        return;
    const QString cleanRoot = cleanAbsPath(root);
    const QString cleanActive = cleanAbsPath(activeWorkspace);
    QSet<QString> kept;
    kept.reserve(chats.size() + 1);
    for (const SavedChat &chat : chats)
        kept.insert(cleanAbsPath(chat.workspace));
    if (!cleanActive.isEmpty())
        kept.insert(cleanActive);

    for (const QFileInfo &entry : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dirPath = QDir::cleanPath(entry.absoluteFilePath());
        if (!kept.contains(dirPath) && isSafeRemovableChatDirClean(cleanRoot, dirPath, cleanActive))
            QDir(dirPath).removeRecursively();
    }
}
}  // namespace

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

    const QJsonArray array = doc.isArray() ? doc.array() : doc.object().value(QLatin1String("chats")).toArray();
    const QString rootDir = chatsDir(stateDir);
    chats.reserve(array.size());
    QSet<QString> seen;
    seen.reserve(array.size());

    for (const QJsonValue &val : array) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString id = obj.value(QLatin1String("id")).toString().trimmed();
        if (!isValidChatId(id) || seen.contains(id))
            continue;
        seen.insert(id);

        SavedChat chat;
        chat.id = id;
        chat.workspace = obj.value(QLatin1String("workspace")).toString().trimmed();
        if (chat.workspace.isEmpty())
            chat.workspace = rootDir + QLatin1Char('/') + id;
        chat.model = obj.value(QLatin1String("model")).toString().trimmed();
        chat.updatedAt = obj.value(QLatin1String("updatedAt")).toInteger(0);
        const QJsonArray turnsArr = obj.value(QLatin1String("turns")).toArray();
        chat.turns.reserve(turnsArr.size());
        for (const QJsonValue &tv : turnsArr) {
            if (!tv.isObject())
                continue;
            const QJsonObject to = tv.toObject();
            QString role = to.value(QLatin1String("role")).toString();
            if (role.isEmpty())
                continue;
            chat.turns.append({
                std::move(role),
                to.value(QLatin1String("text")).toString(),
                to.value(QLatin1String("state")).toString(),
                to.value(QLatin1String("note")).toString(),
            });
        }
        chat.title = obj.value(QLatin1String("title")).toString().trimmed();
        if (chat.title.isEmpty())
            chat.title = firstUserTitle(chat.turns);
        chats.append(std::move(chat));
    }
    sortChatsNewestFirst(chats);
    return chats;
}

QList<ChatSummary> History::loadSummaries(const QString &stateDir) {
    QList<ChatSummary> chats;
    QFile file(indexPath(stateDir));
    if (!file.open(QIODevice::ReadOnly))
        return chats;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return chats;

    const QJsonArray array = doc.isArray() ? doc.array() : doc.object().value(QLatin1String("chats")).toArray();
    const QString rootDir = chatsDir(stateDir);
    chats.reserve(array.size());
    QSet<QString> seen;
    seen.reserve(array.size());

    for (const QJsonValue &val : array) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString id = obj.value(QLatin1String("id")).toString().trimmed();
        if (!isValidChatId(id) || seen.contains(id))
            continue;
        seen.insert(id);

        ChatSummary chat;
        chat.id = id;
        chat.workspace = obj.value(QLatin1String("workspace")).toString().trimmed();
        if (chat.workspace.isEmpty())
            chat.workspace = rootDir + QLatin1Char('/') + id;
        chat.model = obj.value(QLatin1String("model")).toString().trimmed();
        chat.updatedAt = obj.value(QLatin1String("updatedAt")).toInteger(0);
        const QJsonArray turnsArr = obj.value(QLatin1String("turns")).toArray();
        int turnCount = 0;
        for (const QJsonValue &tv : turnsArr) {
            if (tv.isObject() && !tv.toObject().value(QLatin1String("role")).toString().isEmpty())
                ++turnCount;
        }
        chat.turnCount = turnCount;
        chat.title = obj.value(QLatin1String("title")).toString().trimmed();
        if (chat.title.isEmpty())
            chat.title = firstUserTitleFromJson(turnsArr);
        chats.append(std::move(chat));
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
    const QString root = chatsDir(stateDir);
    if (chat.workspace.isEmpty() && isValidChatId(chat.id))
        chat.workspace = root + QLatin1Char('/') + chat.id;
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
        if (chats.at(i).id == chat.id || QDir::cleanPath(chats.at(i).workspace) == cleanWorkspace
            || !QFileInfo(chats.at(i).workspace).isDir()) {
            chats.removeAt(i);
        }
    }
    if (QFileInfo(chat.workspace).isDir())
        chats.prepend(chat);
    sortChatsNewestFirst(chats);

    const QString cleanRoot = cleanAbsPath(root);
    const QString cleanActive = cleanAbsPath(chat.workspace);
    while (chats.size() > kMaxChats) {
        const SavedChat overflow = chats.takeLast();
        if (isSafeRemovableChatDirClean(cleanRoot, cleanAbsPath(overflow.workspace), cleanActive))
            QDir(overflow.workspace).removeRecursively();
    }

    const bool ok = save(stateDir, chats);
    removeOrphanDirs(root, chats, chat.workspace);
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

bool History::remove(const QString &stateDir, const QString &id, QString *removedWorkspace) {
    if (!isValidChatId(id))
        return false;
    QList<SavedChat> chats = load(stateDir);
    QStringList workspacesToDelete;
    for (int i = chats.size() - 1; i >= 0; --i) {
        if (chats.at(i).id != id)
            continue;
        workspacesToDelete.append(chats.takeAt(i).workspace);
    }
    if (workspacesToDelete.isEmpty())
        return false;
    if (!save(stateDir, chats))
        return false;
    if (removedWorkspace && removedWorkspace->isEmpty())
        *removedWorkspace = workspacesToDelete.first();
    const QString cleanRoot = cleanAbsPath(chatsDir(stateDir));
    for (const QString &workspace : workspacesToDelete) {
        if (isSafeRemovableChatDirClean(cleanRoot, cleanAbsPath(workspace), {}))
            QDir(workspace).removeRecursively();
    }
    return true;
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

    const QString cleanRoot = cleanAbsPath(root);
    const QString cleanActive = cleanAbsPath(activeWorkspace);
    while (chats.size() > kMaxChats) {
        const SavedChat overflow = chats.takeLast();
        if (isSafeRemovableChatDirClean(cleanRoot, cleanAbsPath(overflow.workspace), cleanActive))
            QDir(overflow.workspace).removeRecursively();
        modified = true;
    }

    if (modified && (indexExists || !chats.isEmpty()))
        save(stateDir, chats);

    if (rootExists)
        removeOrphanDirs(root, chats, activeWorkspace);
}
