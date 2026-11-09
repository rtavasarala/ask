#include "messagesmodel.h"

#include <QRegularExpression>

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
    }
    return {};
}

QHash<int, QByteArray> MessagesModel::roleNames() const {
    return {{RoleRole, "role"}, {TextRole, "text"}, {DisplayRole, "display"}, {StateRole, "state"}};
}

QString MessagesModel::sanitize(const QString &markdown) {
    // Inline images, reference-style images, and raw <img> tags. The last
    // pattern also eats a tag still being streamed in, which has no ">" yet.
    static const QRegularExpression inlineImage(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression referenceImage(R"(!\[([^\]]*)\]\[[^\]]*\])");
    static const QRegularExpression htmlImage(R"(<img\b[^>]*(>|$))",
                                              QRegularExpression::CaseInsensitiveOption);
    QString out = markdown;
    out.replace(inlineImage, "\\1");
    out.replace(referenceImage, "\\1");
    out.remove(htmlImage);
    return out;
}

void MessagesModel::append(const QString &role, const QString &text, const QString &state) {
    const bool hadText = lastHasText();
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append({role, text, sanitize(text), state});
    endInsertRows();
    emit countChanged();
    if (hadText != lastHasText())
        emit lastHasTextChanged();
}

void MessagesModel::appendToLast(const QString &chunk) {
    if (m_rows.isEmpty() || chunk.isEmpty())
        return;
    const bool hadText = lastHasText();
    Row &row = m_rows.last();
    row.text += chunk;
    row.display = sanitize(row.text);
    const QModelIndex i = index(m_rows.size() - 1);
    emit dataChanged(i, i, {TextRole, DisplayRole});
    if (hadText != lastHasText())
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
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
    if (hadText)
        emit lastHasTextChanged();
}

QString MessagesModel::lastText(const QString &role) const {
    for (auto it = m_rows.crbegin(); it != m_rows.crend(); ++it)
        if (it->role == role)
            return it->text;
    return {};
}
