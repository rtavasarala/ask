#include "attachment.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStringDecoder>
#include <QStringList>

QString appDisplayName(const QString &windowClass) {
    const QStringList parts = windowClass.split('.', Qt::SkipEmptyParts);
    QString name = parts.isEmpty() ? QString() : parts.last().trimmed();
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

QString oneLinePreview(const QString &text, int limit) {
    const QString flat = text.simplified();
    return flat.size() <= limit ? flat : flat.left(limit - 1).trimmed() + "…";
}

std::optional<Attachment> attachmentFromLocalFile(const QString &path, QString *errorOut) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile()) {
        if (errorOut)
            *errorOut = QStringLiteral("File not found: ") + path;
        return std::nullopt;
    }
    if (QImageReader(path).canRead()) {
        Attachment a;
        a.kind = Attachment::Image;
        a.label = info.fileName();
        a.tooltip = path;
        a.payload = path;
        a.owned = false;
        return a;
    }
    if (info.size() > Attachment::kMaxSourceFileBytes) {
        if (errorOut)
            *errorOut = QStringLiteral("File is larger than 5 MiB: ") + path;
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read file: ") + path;
        return std::nullopt;
    }
    const QByteArray raw = file.read(Attachment::kMaxFileBytes);
    if (raw.contains('\0')) {
        if (errorOut)
            *errorOut = QStringLiteral("Binary files are not supported: ") + path;
        return std::nullopt;
    }
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString text = decoder(raw);
    if (decoder.hasError()) {
        if (errorOut)
            *errorOut = QStringLiteral("File is not valid UTF-8 text: ") + path;
        return std::nullopt;
    }
    Attachment a;
    a.kind = Attachment::File;
    a.label = info.fileName();
    a.tooltip = path;
    a.title = info.fileName();
    a.payload = text;
    a.owned = false;
    return a;
}
