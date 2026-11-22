#pragma once

#include <QChar>
#include <QList>
#include <QString>
#include <QStringView>

// Splits Markdown into alternating prose and CommonMark fenced code blocks so
// code can render on its own surface and be copied verbatim without fences.
struct MarkdownBlock {
    bool isCode = false;
    QString language;
    QString body;
};

struct MarkdownFence {
    QChar character;
    int length = 0;
    int indent = 0;
    QString language;
};

bool matchOpeningFence(QStringView line, MarkdownFence &out);
bool matchClosingFence(QStringView line, QChar fenceChar, int minLength);

QList<MarkdownBlock> parseMarkdownBlocks(const QString &markdown);
QString extractLastCodeBlock(const QString &markdown);
