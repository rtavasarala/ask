#include "prompt.h"

#include <QRegularExpression>

namespace {
QString fenceFor(const QString &text) {
    int longest = 0, run = 0;
    for (const QChar c : text) {
        run = c == '`' ? run + 1 : 0;
        longest = qMax(longest, run);
    }
    return QString(qMax(3, longest + 1), '`');
}

QString appOrFallback(const QString &app) { return app.isEmpty() ? QStringLiteral("the active window") : app; }

QString oneLineQuoted(const QString &title) {
    QString flat = title.simplified();
    flat.replace('"', '\'');
    return '"' + flat + '"';
}
}

AssembledPrompt assemblePrompt(const QString &question, const QList<Attachment> &attachments) {
    AssembledPrompt out;
    QStringList blocks;
    for (const Attachment &a : attachments) {
        if (!a.enabled)
            continue;
        if (a.kind == Attachment::Selection) {
            const QString fence = fenceFor(a.payload);
            blocks << QString("Selected text from %1:\n\n%2\n%3\n%2").arg(appOrFallback(a.app), fence, a.payload);
        } else if (a.kind == Attachment::Screen && !a.payload.isEmpty()) {
            QString line = QString("The first attached image is a screenshot of the %1 window").arg(appOrFallback(a.app));
            if (!a.title.simplified().isEmpty())
                line += " titled " + oneLineQuoted(a.title);
            blocks << line + ".";
            out.images << a.payload;
        } else if (a.kind == Attachment::Image && !a.payload.isEmpty()) {
            out.images << a.payload;
        }
    }
    blocks << question.trimmed();
    out.text = blocks.join("\n\n");
    return out;
}
