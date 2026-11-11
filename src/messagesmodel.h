#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariant>

// The conversation as QML sees it. `text` is the raw Markdown from the model;
// `display` and `blocks` are what the view renders, after the sanitizer.
class MessagesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool lastHasText READ lastHasText NOTIFY lastHasTextChanged)
    Q_PROPERTY(bool hasLastCodeBlock READ hasLastCodeBlock NOTIFY lastHasTextChanged)

public:
    enum Roles { RoleRole = Qt::UserRole + 1, TextRole, DisplayRole, StateRole, NoteRole, BlocksRole };

    struct Turn {
        QString role, text, state, note;
    };

    explicit MessagesModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Model output is untrusted: images in prose become their alt text and raw
    // <img> tags are dropped, so rendering never fetches a remote resource.
    static QString sanitize(const QString &markdown);

    // `note` says what a question carried (a selection, a window), for display only.
    void append(const QString &role, const QString &text, const QString &state = {}, const QString &note = {});
    void appendToLast(const QString &chunk);
    void setLastState(const QString &state);
    void clear();

    QList<Turn> turns() const;
    void setTurns(const QList<Turn> &turns);
    QString removeLastTurn();

    int count() const { return m_rows.size(); }
    bool lastHasText() const { return !m_rows.isEmpty() && !m_rows.last().text.isEmpty(); }
    QString lastText(const QString &role) const;
    QString lastCodeBlock() const;
    bool hasLastCodeBlock() const { return !lastCodeBlock().isEmpty(); }

signals:
    void countChanged();
    void lastHasTextChanged();

private:
    struct Row {
        QString role, text, display, state, note, lastCode;
        QVariantList blocks;
    };
    static Row makeRow(const QString &role, const QString &text, const QString &state, const QString &note);
    static void updateDerived(Row &row);

    QList<Row> m_rows;
};
