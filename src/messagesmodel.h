#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariant>
#include <memory>
#include <vector>

#include "markdown.h"

// Per-message block list so streaming updates touch only the active tail block.
class MessageBlocksModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { IsCodeRole = Qt::UserRole + 1, LanguageRole, BodyRole };

    explicit MessageBlocksModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBlocks(QList<MarkdownBlock> blocks);
    void syncFrom(int startIndex, const QList<MarkdownBlock> &tail);
    QVariantList toVariantList() const;

signals:
    void countChanged();

private:
    QList<MarkdownBlock> m_blocks;
};

// The conversation as QML sees it. `text` is the raw Markdown from the model;
// `display`, `blocks`, and `blocksModel` are what the view renders, after the sanitizer.
class MessagesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool lastHasText READ lastHasText NOTIFY lastHasTextChanged)
    Q_PROPERTY(bool hasLastCodeBlock READ hasLastCodeBlock NOTIFY lastHasTextChanged)

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,
        TextRole,
        DisplayRole,
        StateRole,
        NoteRole,
        BlocksRole,
        BlocksModelRole,
        HasTextRole,
    };

    struct Turn {
        QString role, text, state, note;
    };

    explicit MessagesModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    ~MessagesModel() override = default;

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

    int count() const { return int(m_rows.size()); }
    bool lastHasText() const { return !m_rows.empty() && !m_rows.back()->text.isEmpty(); }
    QString lastText(const QString &role) const;
    QString lastCodeBlock() const;
    bool hasLastCodeBlock() const { return !lastCodeBlock().isEmpty(); }

signals:
    void countChanged();
    void lastHasTextChanged();

private:
    struct ParseState {
        qsizetype lineStart = 0;
        bool inCode = false;
        MarkdownFence activeFence;
        QString activeProse;
        bool activeProseHasImageCandidate = false;
        QString activeCode;
        int finalizedBlockCount = 0;
        QString finalizedLastCode;
    };

    struct Row {
        QString role, text, state, note, lastCode;
        mutable QString display;
        mutable bool displayDirty = true;
        ParseState parse;
        MessageBlocksModel *blocksModel = nullptr;
    };

    std::unique_ptr<Row> makeRow(const QString &role, const QString &text, const QString &state, const QString &note);
    void advanceRow(Row &row);

    std::vector<std::unique_ptr<Row>> m_rows;
};
