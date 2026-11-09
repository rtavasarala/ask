#pragma once

#include <QAbstractListModel>
#include <QList>

// The conversation as QML sees it. `text` is the raw Markdown from the model;
// `display` is what the view renders, after the sanitizer.
class MessagesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool lastHasText READ lastHasText NOTIFY lastHasTextChanged)

public:
    enum Roles { RoleRole = Qt::UserRole + 1, TextRole, DisplayRole, StateRole };

    explicit MessagesModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Model output is untrusted: images become their alt text and raw <img>
    // tags are dropped, so rendering never fetches a remote resource.
    static QString sanitize(const QString &markdown);

    void append(const QString &role, const QString &text, const QString &state = {});
    void appendToLast(const QString &chunk);
    void setLastState(const QString &state);
    void clear();

    int count() const { return m_rows.size(); }
    bool lastHasText() const { return !m_rows.isEmpty() && !m_rows.last().text.isEmpty(); }
    QString lastText(const QString &role) const;

signals:
    void countChanged();
    void lastHasTextChanged();

private:
    struct Row {
        QString role, text, display, state;
    };
    QList<Row> m_rows;
};
