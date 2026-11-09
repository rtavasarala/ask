#include "attachment.h"

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
