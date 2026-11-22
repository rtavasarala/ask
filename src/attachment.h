#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

// One chip under the input: something the next question will carry.
struct Attachment {
    enum Kind { Selection = 0, Screen = 1, Image = 2, File = 3 };
    enum State { Ready, Capturing, Failed };

    static constexpr int kMaxFileBytes = 64 * 1024;
    static constexpr qint64 kMaxSourceFileBytes = 5 * 1024 * 1024;

    qint64 id = 0;  // stable across row moves, so an async capture finds its chip
    Kind kind = Selection;
    State state = Ready;
    // Selection: a one-line preview. Screen: the app name. Image/File: the file name.
    // All come from outside, so views must render them as plain text.
    QString label;
    QString tooltip;
    QString app;      // display name of the source app (Selection, Screen)
    QString title;    // window title (Screen) or file name/path (File)
    QString payload;  // Selection/File: the text. Screen and Image: a file path.
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
// Builds an Image or UTF-8 File attachment for a local file, or nullopt if unsupported.
std::optional<Attachment> attachmentFromLocalFile(const QString &path, QString *errorOut = nullptr);
