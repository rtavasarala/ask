#pragma once

#include <QString>
#include <QtGlobal>

// One chip under the input: something the next question will carry.
struct Attachment {
    enum Kind { Selection, Screen, Image };
    enum State { Ready, Capturing, Failed };

    qint64 id = 0;  // stable across row moves, so an async capture finds its chip
    Kind kind = Selection;
    State state = Ready;
    // Selection: a one-line preview. Screen: the app name. Image: the file name.
    // All three come from outside, so views must render them as plain text.
    QString label;
    QString tooltip;
    QString app;      // display name of the source app (Selection, Screen)
    QString title;    // window title (Screen)
    QString payload;  // Selection: the text. Screen and Image: a file path.
    bool enabled = true;
    // The file is ours (runtime dir) and goes away when the request is done.
    // Files the user dropped in are never owned, so never deleted.
    bool owned = false;
    // Screen only: the window to capture, so the capture can start lazily.
    QString stableId;

    bool isContext() const { return kind == Selection || kind == Screen; }
};

// "org.mozilla.firefox" -> "Firefox", "Alacritty" -> "Alacritty", "" -> "".
QString appDisplayName(const QString &windowClass);
// Whitespace collapsed to single spaces, cut to `limit` characters with an ellipsis.
QString oneLinePreview(const QString &text, int limit = 60);
