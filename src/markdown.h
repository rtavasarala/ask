#pragma once

#include <QList>
#include <QString>

// Splits Markdown into alternating prose and CommonMark fenced code blocks so
// code can render on its own surface and be copied verbatim without fences.
struct MarkdownBlock {
    bool isCode = false;
    QString language;
    QString body;
};

QList<MarkdownBlock> parseMarkdownBlocks(const QString &markdown);
QString extractLastCodeBlock(const QString &markdown);
