#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

Settings Settings::load(const QString &path) {
    Settings settings;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return settings;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonValue model = object.value("model");
    if (model.isString())
        settings.model = model.toString().trimmed();
    const QJsonValue effort = object.value("effort");
    if (effort.isString())
        settings.effort = effort.toString().trimmed();
    const QJsonValue fast = object.value("fast");
    if (fast.isBool())
        settings.fast = fast.toBool();
    return settings;
}

bool Settings::save(const QString &path) const {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject object{
        {"model", model},
        {"effort", effort},
        {"fast", fast},
    };
    file.write(QJsonDocument(object).toJson());
    return file.commit();
}

QString Settings::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/ask/settings.json";
}

QList<ModelInfo> parseModelInfos(const QByteArray &output) {
    const QByteArray trimmed = output.trimmed();
    if (trimmed.startsWith('{')) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            QList<ModelInfo> infos;
            QSet<QString> seen;
            for (const QJsonValue &val : root.value("models").toArray()) {
                if (!val.isObject())
                    continue;
                const QJsonObject obj = val.toObject();
                const QString id = obj.value("id").toString().trimmed();
                if (id.isEmpty() || id.contains(' ') || seen.contains(id))
                    continue;
                seen.insert(id);
                ModelInfo info;
                info.id = id;
                const QString name = obj.value("name").toString().trimmed();
                info.name = name.isEmpty() ? shortModelName(id) : name;
                for (const QJsonValue &ev : obj.value("efforts").toArray()) {
                    const QString e = ev.toString().trimmed();
                    if (!e.isEmpty() && !info.efforts.contains(e))
                        info.efforts << e;
                }
                info.fast = obj.value("fast").toBool(false);
                info.ultrafast = obj.value("ultrafast").toBool(false);
                infos << info;
            }
            if (infos.isEmpty()) {
                for (const QJsonValue &val : root.value("ids").toArray()) {
                    const QString id = val.toString().trimmed();
                    if (id.isEmpty() || id.contains(' ') || seen.contains(id))
                        continue;
                    seen.insert(id);
                    infos << ModelInfo{id, shortModelName(id), {}, false, false};
                }
            }
            return infos;
        }
    }

    QList<ModelInfo> infos;
    QSet<QString> seen;
    for (const QByteArray &raw : output.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (!line.startsWith("- "))
            continue;
        const QString id = QString::fromUtf8(line.mid(2)).trimmed();
        if (!id.isEmpty() && !id.contains(' ') && !seen.contains(id)) {
            seen.insert(id);
            infos << ModelInfo{id, shortModelName(id), {}, false, false};
        }
    }
    return infos;
}

QStringList parseModels(const QByteArray &output) {
    const QList<ModelInfo> infos = parseModelInfos(output);
    QStringList ids;
    ids.reserve(infos.size());
    for (const ModelInfo &info : infos)
        ids << info.id;
    return ids;
}

QString shortModelName(const QString &id) { return id.section('/', -1); }

QString resolveFxExecutable() {
    const QString found = QStandardPaths::findExecutable("fx");
    if (!found.isEmpty())
        return found;
    const QString fallback = QDir::homePath() + "/.local/bin/fx";
    return QFileInfo(fallback).isExecutable() ? fallback : QString();
}
