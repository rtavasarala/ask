#pragma once

#include <QList>
#include <QUrl>
#include <functional>

class QWindow;

// Abstract file chooser so tests can inject chosen URLs without touching D-Bus.
class FilePicker {
public:
    virtual ~FilePicker() = default;
    virtual void openFiles(QWindow *parent, std::function<void(const QList<QUrl> &)> done) = 0;
};
