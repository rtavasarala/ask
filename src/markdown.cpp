#include "markdown.h"

#include <QStringView>

namespace {
QString trimSingleTrailingNewline(QString text) {
    if (text.endsWith(QLatin1String("\r\n")))
        text.chop(2);
    else if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    return text;
}

struct FenceOpen {
    QChar character;
    int length = 0;
    int indent = 0;
    QString language;
};

// CommonMark §4.5: up to 3 leading spaces, 3+ matching backticks or tildes,
// and no backticks in the info string of a backtick fence.
bool matchOpeningFence(QStringView line, FenceOpen &out) {
    int indent = 0;
    while (indent < line.size() && indent < 4 && line.at(indent) == QLatin1Char(' '))
        ++indent;
    if (indent >= 4 || indent >= line.size())
        return false;

    const QChar ch = line.at(indent);
    if (ch != QLatin1Char('`') && ch != QLatin1Char('~'))
        return false;

    int count = 0;
    while (indent + count < line.size() && line.at(indent + count) == ch)
        ++count;
    if (count < 3)
        return false;

    const QStringView info = line.mid(indent + count);
    if (ch == QLatin1Char('`') && info.contains(QLatin1Char('`')))
        return false;

    const QStringView trimmed = info.trimmed();
    int tokenEnd = 0;
    while (tokenEnd < trimmed.size() && !trimmed.at(tokenEnd).isSpace())
        ++tokenEnd;

    out.character = ch;
    out.length = count;
    out.indent = indent;
    out.language = trimmed.left(tokenEnd).toString();
    return true;
}

// CommonMark §4.5: up to 3 leading spaces, at least as many matching fence
// characters as the opening fence, and only optional spaces or tabs after.
bool matchClosingFence(QStringView line, QChar fenceChar, int minLength) {
    int indent = 0;
    while (indent < line.size() && indent < 4 && line.at(indent) == QLatin1Char(' '))
        ++indent;
    if (indent >= 4 || indent >= line.size() || line.at(indent) != fenceChar)
        return false;

    int count = 0;
    while (indent + count < line.size() && line.at(indent + count) == fenceChar)
        ++count;
    if (count < minLength)
        return false;

    const QStringView rest = line.mid(indent + count);
    for (QChar c : rest) {
        if (c != QLatin1Char(' ') && c != QLatin1Char('\t'))
            return false;
    }
    return true;
}
}  // namespace

QList<MarkdownBlock> parseMarkdownBlocks(const QString &markdown) {
    if (markdown.isEmpty())
        return {};

    QList<MarkdownBlock> blocks;
    QString prose;
    QString code;
    bool inCode = false;
    FenceOpen activeFence;

    qsizetype pos = 0;
    while (pos < markdown.size()) {
        const qsizetype nl = markdown.indexOf(QLatin1Char('\n'), pos);
        const QStringView rawLine = nl < 0 ? QStringView(markdown).mid(pos)
                                           : QStringView(markdown).mid(pos, nl - pos + 1);
        QStringView line = nl < 0 ? rawLine : rawLine.chopped(1);
        if (line.endsWith(QLatin1Char('\r')))
            line = line.chopped(1);
        pos = nl < 0 ? markdown.size() : nl + 1;

        if (!inCode) {
            FenceOpen opened;
            if (matchOpeningFence(line, opened)) {
                const QString proseBody = trimSingleTrailingNewline(prose);
                if (!proseBody.trimmed().isEmpty())
                    blocks.append({false, {}, proseBody});
                prose.clear();
                inCode = true;
                activeFence = opened;
                code.clear();
            } else {
                prose.append(rawLine);
            }
        } else {
            if (matchClosingFence(line, activeFence.character, activeFence.length)) {
                blocks.append({true, activeFence.language, trimSingleTrailingNewline(code)});
                inCode = false;
                activeFence = {};
                code.clear();
            } else {
                int strip = 0;
                while (strip < activeFence.indent && strip < rawLine.size() && rawLine.at(strip) == QLatin1Char(' '))
                    ++strip;
                code.append(rawLine.mid(strip));
            }
        }
    }

    if (inCode) {
        blocks.append({true, activeFence.language, trimSingleTrailingNewline(code)});
    } else {
        const QString proseBody = trimSingleTrailingNewline(prose);
        if (!proseBody.trimmed().isEmpty())
            blocks.append({false, {}, proseBody});
    }

    return blocks;
}

QString extractLastCodeBlock(const QString &markdown) {
    const QList<MarkdownBlock> blocks = parseMarkdownBlocks(markdown);
    for (auto it = blocks.crbegin(); it != blocks.crend(); ++it) {
        if (it->isCode && !it->body.isEmpty())
            return it->body;
    }
    return {};
}
