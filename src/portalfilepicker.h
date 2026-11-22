#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "filepicker.h"

// Tracks one in-flight xdg-desktop-portal Request until its Response signal arrives.
class PortalFileRequest : public QObject {
    Q_OBJECT

public:
    PortalFileRequest(std::function<void(const QList<QUrl> &)> done, const QString &requestPath,
                      QObject *parent = nullptr);

    bool isSettled() const { return m_settled; }
    const QString &requestPath() const { return m_requestPath; }
    void rebindPath(const QString &actualPath);
    void finish(const QList<QUrl> &urls);

public slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    std::function<void(const QList<QUrl> &)> m_done;
    QString m_requestPath;
    bool m_settled = false;
};

// Opens the desktop's native file chooser over org.freedesktop.portal.FileChooser.
class PortalFilePicker : public QObject, public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    void openFiles(QWindow *parent, std::function<void(const QList<QUrl> &)> done) override;

private:
    quint64 m_counter = 0;
};
