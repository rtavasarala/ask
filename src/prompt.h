#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "attachment.h"

struct AssembledPrompt {
    QString text;
    QStringList images;  // in the order fx receives them: screen first, then images as added
};

// System prompt telling fx to treat external context as untrusted data.
const char *askSystemPrompt();

// Pure: the question plus the attachments the user left enabled. Selections,
// attached files, and window titles are untrusted, so text blocks sit in a code
// fence longer than any backtick run inside them, and names are flattened to one
// quoted line.
AssembledPrompt assemblePrompt(const QString &question, const QList<Attachment> &attachments);
