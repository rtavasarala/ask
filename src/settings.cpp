#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
    return settings;
}

bool Settings::save(const QString &path) const {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonObject{{"model", model}}).toJson());
    return file.commit();
}

QString Settings::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/ask/settings.json";
}

QStringList parseModels(const QByteArray &output) {
    QStringList models;
    for (const QByteArray &raw : output.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (!line.startsWith("- "))
            continue;
        const QString id = QString::fromUtf8(line.mid(2)).trimmed();
        if (!id.isEmpty() && !id.contains(' ') && !models.contains(id))
            models << id;
    }
    return models;
}

QString shortModelName(const QString &id) { return id.section('/', -1); }
