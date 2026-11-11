#include "messagesmodel.h"

#include <QRegularExpression>
#include <QStringView>

#include "markdown.h"

namespace {
QString sanitizePlainSegment(const QString &segment) {
    static const QRegularExpression inlineImage(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression referenceImage(R"(!\[([^\]]*)\]\[[^\]]*\])");
    static const QRegularExpression htmlImage(R"(<img\b[^>]*(>|$))",
                                              QRegularExpression::CaseInsensitiveOption);
    QString out = segment;
    out.replace(inlineImage, "\\1");
    out.replace(referenceImage, "\\1");
    out.remove(htmlImage);
    return out;
}

QString sanitizeProse(const QString &markdown) {
    // Preserve closed inline code spans (`...`) verbatim while stripping
    // Markdown and HTML images from surrounding prose.
    static const QRegularExpression inlineCode(R"((`+)(?!`)(?:[\s\S]*?[^`])?\1(?!`))");
    QString out;
    out.reserve(markdown.size());
    qsizetype last = 0;
    auto it = inlineCode.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedStart() > last)
            out.append(sanitizePlainSegment(markdown.mid(last, match.capturedStart() - last)));
        out.append(match.captured(0));
        last = match.capturedEnd();
    }
    if (last < markdown.size())
        out.append(sanitizePlainSegment(markdown.mid(last)));
    return out;
}

bool isFenceStart(QStringView line, QChar &fenceChar, int &fenceLen) {
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
    fenceChar = ch;
    fenceLen = count;
    return true;
}

bool isFenceEnd(QStringView line, QChar fenceChar, int minLen) {
    int indent = 0;
    while (indent < line.size() && indent < 4 && line.at(indent) == QLatin1Char(' '))
        ++indent;
    if (indent >= 4 || indent >= line.size() || line.at(indent) != fenceChar)
        return false;
    int count = 0;
    while (indent + count < line.size() && line.at(indent + count) == fenceChar)
        ++count;
    if (count < minLen)
        return false;
    for (QChar c : line.mid(indent + count)) {
        if (c != QLatin1Char(' ') && c != QLatin1Char('\t'))
            return false;
    }
    return true;
}
}  // namespace

int MessagesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RoleRole: return row.role;
    case TextRole: return row.text;
    case DisplayRole: return row.display;
    case StateRole: return row.state;
    case NoteRole: return row.note;
    case BlocksRole: return row.blocks;
    }
    return {};
}

QHash<int, QByteArray> MessagesModel::roleNames() const {
    return {
        {RoleRole, "role"},
        {TextRole, "text"},
        {DisplayRole, "display"},
        {StateRole, "state"},
        {NoteRole, "note"},
        {BlocksRole, "blocks"},
    };
}

QString MessagesModel::sanitize(const QString &markdown) {
    if (markdown.isEmpty())
        return {};

    // Strip Markdown and HTML images only in prose so code blocks keep literal
    // `<img>` tags or Markdown examples intact.
    QString out;
    out.reserve(markdown.size());
    QString proseChunk;
    bool inCode = false;
    QChar fenceChar;
    int fenceLen = 0;

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
            if (isFenceStart(line, fenceChar, fenceLen)) {
                if (!proseChunk.isEmpty()) {
                    out.append(sanitizeProse(proseChunk));
                    proseChunk.clear();
                }
                out.append(rawLine);
                inCode = true;
            } else {
                proseChunk.append(rawLine);
            }
        } else {
            out.append(rawLine);
            if (isFenceEnd(line, fenceChar, fenceLen))
                inCode = false;
        }
    }

    if (!proseChunk.isEmpty())
        out.append(sanitizeProse(proseChunk));
    return out;
}

MessagesModel::Row MessagesModel::makeRow(const QString &role, const QString &text, const QString &state, const QString &note) {
    Row row;
    row.role = role;
    row.text = text;
    row.state = state;
    row.note = note;
    updateDerived(row);
    return row;
}

void MessagesModel::updateDerived(Row &row) {
    row.display = sanitize(row.text);
    row.blocks.clear();
    row.lastCode.clear();
    const QList<MarkdownBlock> parsed = parseMarkdownBlocks(row.text);
    row.blocks.reserve(parsed.size());
    for (const MarkdownBlock &b : parsed) {
        if (b.isCode) {
            row.blocks.append(QVariantMap{{"isCode", true}, {"language", b.language}, {"body", b.body}});
            if (!b.body.isEmpty())
                row.lastCode = b.body;
        } else {
            const QString clean = sanitizeProse(b.body);
            if (!clean.trimmed().isEmpty())
                row.blocks.append(QVariantMap{{"isCode", false}, {"language", QString()}, {"body", clean}});
        }
    }
}

void MessagesModel::append(const QString &role, const QString &text, const QString &state, const QString &note) {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append(makeRow(role, text, state, note));
    endInsertRows();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

void MessagesModel::appendToLast(const QString &chunk) {
    if (m_rows.isEmpty() || chunk.isEmpty())
        return;
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    Row &row = m_rows.last();
    row.text += chunk;
    updateDerived(row);
    const QModelIndex i = index(m_rows.size() - 1);
    emit dataChanged(i, i, {TextRole, DisplayRole, BlocksRole});
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

void MessagesModel::setLastState(const QString &state) {
    if (m_rows.isEmpty() || m_rows.last().state == state)
        return;
    m_rows.last().state = state;
    const QModelIndex i = index(m_rows.size() - 1);
    emit dataChanged(i, i, {StateRole});
}

void MessagesModel::clear() {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
    if (hadText || hadCode)
        emit lastHasTextChanged();
}

QList<MessagesModel::Turn> MessagesModel::turns() const {
    QList<Turn> out;
    out.reserve(m_rows.size());
    for (const Row &row : m_rows)
        out.append({row.role, row.text, row.state, row.note});
    return out;
}

void MessagesModel::setTurns(const QList<Turn> &turns) {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(turns.size());
    for (const Turn &turn : turns)
        m_rows.append(makeRow(turn.role, turn.text, turn.state, turn.note));
    endResetModel();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

QString MessagesModel::removeLastTurn() {
    if (m_rows.isEmpty())
        return {};

    int first = m_rows.size();
    QString userText;
    if (m_rows.last().role == QLatin1String("assistant"))
        --first;
    if (first > 0 && m_rows.at(first - 1).role == QLatin1String("user")) {
        --first;
        userText = m_rows.at(first).text;
    }
    if (first == m_rows.size())
        return {};

    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    beginRemoveRows({}, first, m_rows.size() - 1);
    m_rows.remove(first, m_rows.size() - first);
    endRemoveRows();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
    return userText;
}

QString MessagesModel::lastText(const QString &role) const {
    for (auto it = m_rows.crbegin(); it != m_rows.crend(); ++it)
        if (it->role == role)
            return it->text;
    return {};
}

QString MessagesModel::lastCodeBlock() const {
    for (auto it = m_rows.crbegin(); it != m_rows.crend(); ++it)
        if (it->role == QLatin1String("assistant"))
            return it->lastCode;
    return {};
}
