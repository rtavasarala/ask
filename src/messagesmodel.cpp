#include "messagesmodel.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringView>
#include <cstdint>

namespace {
bool hasImageSyntaxCandidate(QStringView text) {
    const QChar *data = text.constData();
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t ch = data[i].unicode();
        if (ch == '!' && i + 1 < n && data[i + 1].unicode() == '[')
            return true;
        if (ch == '<' && i + 3 < n) {
            const char16_t c1 = data[i + 1].unicode() | 32;
            const char16_t c2 = data[i + 2].unicode() | 32;
            const char16_t c3 = data[i + 3].unicode() | 32;
            if (c1 == 'i' && c2 == 'm' && c3 == 'g')
                return true;
        }
    }
    return false;
}

bool hasImageSyntaxAcrossBoundary(QStringView prev, QStringView added) {
    if (hasImageSyntaxCandidate(added))
        return true;
    if (prev.isEmpty() || added.isEmpty())
        return false;
    const qsizetype prevKeep = qMin<qsizetype>(3, prev.size());
    const qsizetype addKeep = qMin<qsizetype>(3, added.size());
    QChar bridge[6];
    for (qsizetype i = 0; i < prevKeep; ++i)
        bridge[i] = prev.at(prev.size() - prevKeep + i);
    for (qsizetype i = 0; i < addKeep; ++i)
        bridge[prevKeep + i] = added.at(i);
    return hasImageSyntaxCandidate(QStringView(bridge, prevKeep + addKeep));
}

QString sanitizePlainSegment(QStringView segment) {
    if (!hasImageSyntaxCandidate(segment))
        return segment.toString();

    static const QRegularExpression inlineImage(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression referenceImage(R"(!\[([^\]]*)\]\[[^\]]*\])");
    static const QRegularExpression htmlImage(R"(<img\b[^>]*(>|$))",
                                              QRegularExpression::CaseInsensitiveOption);
    QString out = segment.toString();
    if (out.contains(QLatin1String("!["))) {
        out.replace(inlineImage, QStringLiteral("\\1"));
        out.replace(referenceImage, QStringLiteral("\\1"));
    }
    if (out.contains(QLatin1String("<img"), Qt::CaseInsensitive))
        out.remove(htmlImage);
    return out;
}

QString sanitizeProse(const QString &markdown) {
    if (!hasImageSyntaxCandidate(markdown))
        return markdown;

    // Linear-time inline code span scanner (`...`) preserving closed code spans
    // verbatim while stripping Markdown and HTML images from surrounding prose.
    const qsizetype n = markdown.size();
    const QChar *data = markdown.constData();
    QString out;
    out.reserve(n);
    qsizetype last = 0;
    uint64_t unclosedSmall = 0;
    QSet<qsizetype> unclosedLarge;

    auto isUnclosed = [&](qsizetype len) -> bool {
        return len < 64 ? ((unclosedSmall & (uint64_t(1) << len)) != 0) : unclosedLarge.contains(len);
    };
    auto markUnclosed = [&](qsizetype len) {
        if (len < 64)
            unclosedSmall |= (uint64_t(1) << len);
        else
            unclosedLarge.insert(len);
    };

    qsizetype i = 0;
    while (i < n) {
        if (data[i].unicode() != '`') {
            ++i;
            continue;
        }
        qsizetype runLen = 0;
        while (i + runLen < n && data[i + runLen].unicode() == '`')
            ++runLen;

        if (isUnclosed(runLen)) {
            i += runLen;
            continue;
        }

        qsizetype j = i + runLen;
        qsizetype closePos = -1;
        while (j < n) {
            if (data[j].unicode() != '`') {
                ++j;
                continue;
            }
            qsizetype m = 0;
            while (j + m < n && data[j + m].unicode() == '`')
                ++m;
            if (m == runLen) {
                closePos = j;
                break;
            }
            j += m;
        }

        if (closePos >= 0) {
            if (i > last)
                out.append(sanitizePlainSegment(QStringView(markdown).mid(last, i - last)));
            const qsizetype spanEnd = closePos + runLen;
            out.append(QStringView(markdown).mid(i, spanEnd - i));
            last = spanEnd;
            i = spanEnd;
        } else {
            markUnclosed(runLen);
            i += runLen;
        }
    }

    if (last < n)
        out.append(sanitizePlainSegment(QStringView(markdown).mid(last)));
    return out;
}

QString trimSingleTrailingNewline(QString text) {
    if (text.endsWith(QLatin1String("\r\n")))
        text.chop(2);
    else if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    return text;
}

QStringView stripIndent(QStringView rawLine, int maxIndent) {
    int strip = 0;
    while (strip < maxIndent && strip < rawLine.size() && rawLine.at(strip) == QLatin1Char(' '))
        ++strip;
    return rawLine.mid(strip);
}
}  // namespace

int MessageBlocksModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_blocks.size();
}

QVariant MessageBlocksModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
        return {};
    const MarkdownBlock &b = m_blocks.at(index.row());
    switch (role) {
    case IsCodeRole: return b.isCode;
    case LanguageRole: return b.language;
    case BodyRole: return b.body;
    }
    return {};
}

QHash<int, QByteArray> MessageBlocksModel::roleNames() const {
    return {
        {IsCodeRole, "isCode"},
        {LanguageRole, "language"},
        {BodyRole, "body"},
    };
}

void MessageBlocksModel::setBlocks(QList<MarkdownBlock> blocks) {
    const int oldCount = m_blocks.size();
    beginResetModel();
    m_blocks = std::move(blocks);
    endResetModel();
    if (oldCount != m_blocks.size())
        emit countChanged();
}

void MessageBlocksModel::syncFrom(int startIndex, const QList<MarkdownBlock> &tail) {
    const int oldCount = m_blocks.size();
    const int newCount = startIndex + tail.size();
    const int common = qMin(oldCount, newCount);

    for (int i = startIndex; i < common; ++i) {
        const MarkdownBlock &next = tail.at(i - startIndex);
        MarkdownBlock &cur = m_blocks[i];
        QList<int> changed;
        if (cur.isCode != next.isCode) {
            cur.isCode = next.isCode;
            changed << IsCodeRole;
        }
        if (cur.language != next.language) {
            cur.language = next.language;
            changed << LanguageRole;
        }
        if (cur.body != next.body) {
            cur.body = next.body;
            changed << BodyRole;
        }
        if (!changed.isEmpty()) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, changed);
        }
    }

    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        for (int i = oldCount; i < newCount; ++i)
            m_blocks.append(tail.at(i - startIndex));
        endInsertRows();
        emit countChanged();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        m_blocks.remove(newCount, oldCount - newCount);
        endRemoveRows();
        emit countChanged();
    }
}

QVariantList MessageBlocksModel::toVariantList() const {
    QVariantList out;
    out.reserve(m_blocks.size());
    for (const MarkdownBlock &b : m_blocks)
        out.append(QVariantMap{{"isCode", b.isCode}, {"language", b.language}, {"body", b.body}});
    return out;
}

int MessagesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const Row &row = *m_rows.at(size_t(index.row()));
    switch (role) {
    case RoleRole: return row.role;
    case TextRole: return row.text;
    case DisplayRole:
        if (row.displayDirty) {
            row.display = sanitize(row.text);
            row.displayDirty = false;
        }
        return row.display;
    case StateRole: return row.state;
    case NoteRole: return row.note;
    case BlocksRole: return row.blocksModel ? row.blocksModel->toVariantList() : QVariantList{};
    case BlocksModelRole: return QVariant::fromValue<QObject *>(row.blocksModel);
    case HasTextRole: return !row.text.isEmpty();
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
        {BlocksModelRole, "blocksModel"},
        {HasTextRole, "hasText"},
    };
}

QString MessagesModel::sanitize(const QString &markdown) {
    if (markdown.isEmpty() || !hasImageSyntaxCandidate(markdown))
        return markdown;

    // Strip Markdown and HTML images only in prose so code blocks keep literal
    // `<img>` tags or Markdown examples intact.
    QString out;
    out.reserve(markdown.size());
    QString proseChunk;
    bool inCode = false;
    MarkdownFence fence;

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
            MarkdownFence opened;
            if (matchOpeningFence(line, opened)) {
                if (!proseChunk.isEmpty()) {
                    out.append(sanitizeProse(proseChunk));
                    proseChunk.clear();
                }
                out.append(rawLine);
                inCode = true;
                fence = opened;
            } else {
                proseChunk.append(rawLine);
            }
        } else {
            out.append(rawLine);
            if (matchClosingFence(line, fence.character, fence.length))
                inCode = false;
        }
    }

    if (!proseChunk.isEmpty())
        out.append(sanitizeProse(proseChunk));
    return out;
}

std::unique_ptr<MessagesModel::Row> MessagesModel::makeRow(const QString &role, const QString &text,
                                                           const QString &state, const QString &note) {
    auto row = std::make_unique<Row>();
    row->role = role;
    row->text = text;
    row->state = state;
    row->note = note;
    row->displayDirty = true;
    row->blocksModel = new MessageBlocksModel(this);
    if (!text.isEmpty())
        advanceRow(*row);
    return row;
}

void MessagesModel::advanceRow(Row &row) {
    ParseState &st = row.parse;
    const int prevFinalized = st.finalizedBlockCount;
    QList<MarkdownBlock> tail;

    // Process every newly completed line (terminated by '\n') once.
    while (st.lineStart < row.text.size()) {
        const qsizetype nl = row.text.indexOf(QLatin1Char('\n'), st.lineStart);
        if (nl < 0)
            break;

        const QStringView rawLine = QStringView(row.text).mid(st.lineStart, nl - st.lineStart + 1);
        QStringView line = rawLine.chopped(1);
        if (line.endsWith(QLatin1Char('\r')))
            line = line.chopped(1);
        st.lineStart = nl + 1;

        if (!st.inCode) {
            MarkdownFence opened;
            if (matchOpeningFence(line, opened)) {
                const QString proseBody = trimSingleTrailingNewline(st.activeProse);
                const QString clean = st.activeProseHasImageCandidate ? sanitizeProse(proseBody) : proseBody;
                if (!clean.trimmed().isEmpty()) {
                    tail.append({false, {}, clean});
                    ++st.finalizedBlockCount;
                }
                st.activeProse.clear();
                st.activeProseHasImageCandidate = false;
                st.inCode = true;
                st.activeFence = opened;
                st.activeCode.clear();
            } else {
                if (!st.activeProseHasImageCandidate && hasImageSyntaxAcrossBoundary(st.activeProse, rawLine))
                    st.activeProseHasImageCandidate = true;
                st.activeProse.append(rawLine);
            }
        } else {
            if (matchClosingFence(line, st.activeFence.character, st.activeFence.length)) {
                const QString body = trimSingleTrailingNewline(st.activeCode);
                tail.append({true, st.activeFence.language, body});
                ++st.finalizedBlockCount;
                if (!body.isEmpty())
                    st.finalizedLastCode = body;
                st.inCode = false;
                st.activeFence = {};
                st.activeCode.clear();
            } else {
                st.activeCode.append(stripIndent(rawLine, st.activeFence.indent));
            }
        }
    }

    // Build provisional block(s) for the current unfinalized state + trailing partial line.
    row.lastCode = st.finalizedLastCode;
    const QStringView rem = QStringView(row.text).mid(st.lineStart);
    QStringView remLine = rem;
    if (remLine.endsWith(QLatin1Char('\r')))
        remLine = remLine.chopped(1);

    if (!st.inCode) {
        MarkdownFence opened;
        if (!rem.isEmpty() && matchOpeningFence(remLine, opened)) {
            const QString proseBody = trimSingleTrailingNewline(st.activeProse);
            const QString clean = st.activeProseHasImageCandidate ? sanitizeProse(proseBody) : proseBody;
            if (!clean.trimmed().isEmpty())
                tail.append({false, {}, clean});
            tail.append({true, opened.language, {}});
        } else if (!st.activeProse.isEmpty() || !rem.isEmpty()) {
            QString combined = st.activeProse;
            const bool needsSanitize = st.activeProseHasImageCandidate
                                       || (!rem.isEmpty() && hasImageSyntaxAcrossBoundary(st.activeProse, rem));
            if (!rem.isEmpty())
                combined.append(rem);
            const QString proseBody = trimSingleTrailingNewline(std::move(combined));
            const QString clean = needsSanitize ? sanitizeProse(proseBody) : proseBody;
            if (!clean.trimmed().isEmpty())
                tail.append({false, {}, clean});
        }
    } else {
        if (!rem.isEmpty() && matchClosingFence(remLine, st.activeFence.character, st.activeFence.length)) {
            const QString body = trimSingleTrailingNewline(st.activeCode);
            tail.append({true, st.activeFence.language, body});
            if (!body.isEmpty())
                row.lastCode = body;
        } else {
            QString combined = st.activeCode;
            if (!rem.isEmpty())
                combined.append(stripIndent(rem, st.activeFence.indent));
            const QString body = trimSingleTrailingNewline(std::move(combined));
            tail.append({true, st.activeFence.language, body});
            if (!body.isEmpty())
                row.lastCode = body;
        }
    }

    row.blocksModel->syncFrom(prevFinalized, tail);
}

void MessagesModel::append(const QString &role, const QString &text, const QString &state, const QString &note) {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    const int pos = int(m_rows.size());
    beginInsertRows({}, pos, pos);
    m_rows.push_back(makeRow(role, text, state, note));
    endInsertRows();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

void MessagesModel::appendToLast(const QString &chunk) {
    if (m_rows.empty() || chunk.isEmpty())
        return;
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    Row &row = *m_rows.back();
    const bool rowHadText = !row.text.isEmpty();
    row.text += chunk;
    row.displayDirty = true;
    advanceRow(row);
    const QModelIndex i = index(int(m_rows.size()) - 1);
    QList<int> roles{TextRole, DisplayRole, BlocksRole};
    if (!rowHadText)
        roles << HasTextRole;
    emit dataChanged(i, i, roles);
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

void MessagesModel::setLastState(const QString &state) {
    if (m_rows.empty() || m_rows.back()->state == state)
        return;
    m_rows.back()->state = state;
    const QModelIndex i = index(int(m_rows.size()) - 1);
    emit dataChanged(i, i, {StateRole});
}

void MessagesModel::clear() {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    std::vector<MessageBlocksModel *> obsolete;
    obsolete.reserve(m_rows.size());
    for (const auto &row : m_rows)
        if (row->blocksModel)
            obsolete.push_back(row->blocksModel);
    beginResetModel();
    m_rows.clear();
    endResetModel();
    for (MessageBlocksModel *m : obsolete)
        m->deleteLater();
    emit countChanged();
    if (hadText || hadCode)
        emit lastHasTextChanged();
}

QList<MessagesModel::Turn> MessagesModel::turns() const {
    QList<Turn> out;
    out.reserve(qsizetype(m_rows.size()));
    for (const auto &row : m_rows)
        out.append({row->role, row->text, row->state, row->note});
    return out;
}

void MessagesModel::setTurns(const QList<Turn> &turns) {
    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    std::vector<MessageBlocksModel *> obsolete;
    obsolete.reserve(m_rows.size());
    for (const auto &row : m_rows)
        if (row->blocksModel)
            obsolete.push_back(row->blocksModel);
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(size_t(turns.size()));
    for (const Turn &turn : turns)
        m_rows.push_back(makeRow(turn.role, turn.text, turn.state, turn.note));
    endResetModel();
    for (MessageBlocksModel *m : obsolete)
        m->deleteLater();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
}

QString MessagesModel::removeLastTurn() {
    if (m_rows.empty())
        return {};

    int first = int(m_rows.size());
    QString userText;
    if (m_rows.back()->role == QLatin1String("assistant"))
        --first;
    if (first > 0 && m_rows.at(size_t(first - 1))->role == QLatin1String("user")) {
        --first;
        userText = m_rows.at(size_t(first))->text;
    }
    if (first == int(m_rows.size()))
        return {};

    const bool hadText = lastHasText();
    const bool hadCode = hasLastCodeBlock();
    std::vector<MessageBlocksModel *> obsolete;
    for (int idx = first; idx < int(m_rows.size()); ++idx)
        if (m_rows.at(size_t(idx))->blocksModel)
            obsolete.push_back(m_rows.at(size_t(idx))->blocksModel);
    beginRemoveRows({}, first, int(m_rows.size()) - 1);
    m_rows.erase(m_rows.begin() + first, m_rows.end());
    endRemoveRows();
    for (MessageBlocksModel *m : obsolete)
        m->deleteLater();
    emit countChanged();
    if (hadText != lastHasText() || hadCode != hasLastCodeBlock())
        emit lastHasTextChanged();
    return userText;
}

QString MessagesModel::lastText(const QString &role) const {
    for (auto it = m_rows.crbegin(); it != m_rows.crend(); ++it)
        if ((*it)->role == role)
            return (*it)->text;
    return {};
}

QString MessagesModel::lastCodeBlock() const {
    for (auto it = m_rows.crbegin(); it != m_rows.crend(); ++it)
        if ((*it)->role == QLatin1String("assistant"))
            return (*it)->lastCode;
    return {};
}
