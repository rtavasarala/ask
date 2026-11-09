#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "attachment.h"

struct AssembledPrompt {
    QString text;
    QStringList images;  // in the order fx receives them: screen first, then images as added
};

// Pure: the question plus the attachments the user left enabled. Selection and
// window titles are untrusted, so the selection sits in a code fence longer than
// any backtick run inside it, and a title is flattened to one quoted line.
AssembledPrompt assemblePrompt(const QString &question, const QList<Attachment> &attachments);
