#include "attachmentsmodel.h"

QHash<int, QByteArray> AttachmentsModel::roleNames() const {
    return {{KindRole, "kind"}, {LabelRole, "label"}, {TooltipRole, "tooltip"}, {EnabledRole, "checked"}, {StateRole, "phase"}};
}

QVariant AttachmentsModel::data(const QModelIndex &index, int role) const {
    const Attachment *a = index.isValid() ? at(index.row()) : nullptr;
    if (!a)
        return {};
    switch (role) {
    case KindRole: return int(a->kind);
    case LabelRole: return a->label;
    case TooltipRole: return a->tooltip;
    case EnabledRole: return a->enabled;
    case StateRole: return int(a->state);
    }
    return {};
}

bool AttachmentsModel::hasEnabledSelection() const {
    for (const Attachment &a : m_items)
        if (a.kind == Attachment::Selection && a.enabled)
            return true;
    return false;
}

int AttachmentsModel::screenRow() const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).kind == Attachment::Screen)
            return i;
    return -1;
}

int AttachmentsModel::rowOfId(qint64 id) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).id == id)
            return i;
    return -1;
}

void AttachmentsModel::setOwned(int row, bool owned) {
    if (at(row))
        m_items[row].owned = owned;
}

bool AttachmentsModel::hasScreen() const { return screenRow() >= 0; }

void AttachmentsModel::changed(int row, const QList<int> &roles) {
    emit dataChanged(index(row), index(row), roles);
    emit countChanged();
}

QList<Attachment> AttachmentsModel::applyContext(const std::optional<Context> &context) {
    QList<Attachment> replaced;
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items.at(i).isContext())
            replaced.prepend(*removeAt(i));
    if (!context)
        return replaced;

    QList<Attachment> fresh;
    const QString app = appDisplayName(context->appClass);
    if (!context->selection.trimmed().isEmpty()) {
        Attachment a;
        a.id = m_nextId++;
        a.kind = Attachment::Selection;
        a.app = app;
        a.payload = context->selection;
        a.label = oneLinePreview(context->selection);
        a.tooltip = oneLinePreview(context->selection, 400);
        a.enabled = context->selection != m_lastSelection;
        m_lastSelection = context->selection;
        fresh << a;
    }
    if (!context->stableId.isEmpty()) {
        Attachment a;
        a.id = m_nextId++;
        a.kind = Attachment::Screen;
        a.app = app;
        a.title = context->windowTitle;
        a.label = app;
        a.tooltip = context->windowTitle;
        a.stableId = context->stableId;
        a.enabled = false;
        fresh << a;
    }
    if (fresh.isEmpty())
        return replaced;
    // Context chips go first so they sit before the user's images.
    beginInsertRows({}, 0, fresh.size() - 1);
    for (int i = 0; i < fresh.size(); ++i)
        m_items.insert(i, fresh.at(i));
    endInsertRows();
    emit countChanged();
    return replaced;
}

qint64 AttachmentsModel::add(const Attachment &attachment) {
    Attachment stored = attachment;
    stored.id = m_nextId++;
    beginInsertRows({}, m_items.size(), m_items.size());
    m_items << stored;
    endInsertRows();
    emit countChanged();
    return stored.id;
}

std::optional<Attachment> AttachmentsModel::removeAt(int row) {
    if (!at(row))
        return std::nullopt;
    beginRemoveRows({}, row, row);
    const Attachment removed = m_items.takeAt(row);
    endRemoveRows();
    emit countChanged();
    return removed;
}

void AttachmentsModel::setEnabled(int row, bool enabled) {
    if (!at(row) || m_items[row].enabled == enabled)
        return;
    m_items[row].enabled = enabled;
    changed(row, {EnabledRole});
}

void AttachmentsModel::setState(int row, Attachment::State state, const QString &detail) {
    if (!at(row))
        return;
    m_items[row].state = state;
    if (state == Attachment::Failed)
        m_items[row].tooltip = detail;
    changed(row, {StateRole, TooltipRole});
}

void AttachmentsModel::setPayload(int row, const QString &path) {
    if (at(row))
        m_items[row].payload = path;
}

QList<Attachment> AttachmentsModel::takeEnabled() {
    QList<Attachment> taken;
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items.at(i).enabled && m_items.at(i).state == Attachment::Ready)
            taken.prepend(*removeAt(i));
    return taken;
}

QList<Attachment> AttachmentsModel::clear() {
    const QList<Attachment> all = m_items;
    if (!all.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }
    return all;
}
