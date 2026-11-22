#pragma once

#include <QAbstractListModel>
#include <QList>

#include "attachment.h"
#include "context.h"

// The chips under the input. Pure state: Backend owns the work (capturing,
// deleting files, sending).
class AttachmentsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool hasEnabledSelection READ hasEnabledSelection NOTIFY countChanged)
    Q_PROPERTY(bool hasScreen READ hasScreen NOTIFY countChanged)
    Q_PROPERTY(bool screenEnabled READ screenEnabled NOTIFY countChanged)

public:
    enum Roles { KindRole = Qt::UserRole + 1, LabelRole, TooltipRole, EnabledRole, StateRole };

    explicit AttachmentsModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_items.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<Attachment> &items() const { return m_items; }
    const Attachment *at(int row) const { return row >= 0 && row < m_items.size() ? &m_items.at(row) : nullptr; }
    bool hasEnabledSelection() const;
    bool hasScreen() const;
    bool screenEnabled() const { const Attachment *a = at(screenRow()); return a && a->enabled; }
    int screenRow() const;
    int rowOfId(qint64 id) const;
    void setOwned(int row, bool owned);

    // Replaces the context chips of the previous show; chips the user added
    // stay. A selection equal to the one seen at the previous show is stale
    // (primary selection outlives the user's interest), so its chip is shown
    // but disabled.
    // Returns the context chips it replaced so the caller can delete their files.
    QList<Attachment> applyContext(const std::optional<Context> &context);
    qint64 add(const Attachment &attachment);
    // Returns the removed attachment so the caller can delete its file.
    std::optional<Attachment> removeAt(int row);
    void setEnabled(int row, bool enabled);
    void setState(int row, Attachment::State state, const QString &detail = {});
    void setPayload(int row, const QString &path);
    // Removes and returns the chips that a request just consumed.
    QList<Attachment> takeEnabled();
    // Puts unconsumed chips from a failed request back at the front so a retry keeps them.
    void restore(const QList<Attachment> &attachments);
    QList<Attachment> clear();

signals:
    void countChanged();

private:
    void changed(int row, const QList<int> &roles);

    QList<Attachment> m_items;
    QString m_lastSelection;
    qint64 m_nextId = 1;
};
