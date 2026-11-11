#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QMetaObject>
#include <unistd.h>
#include <utility>

namespace {
const auto kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const auto kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const auto kFileChooserIface = QStringLiteral("org.freedesktop.portal.FileChooser");
const auto kRequestIface = QStringLiteral("org.freedesktop.portal.Request");
}

PortalFileRequest::PortalFileRequest(std::function<void(const QList<QUrl> &)> done, const QString &requestPath,
                                     QObject *parent)
    : QObject(parent), m_done(std::move(done)), m_requestPath(requestPath) {}

void PortalFileRequest::rebindPath(const QString &actualPath) {
    if (m_settled || actualPath.isEmpty() || actualPath == m_requestPath)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.disconnect(kPortalService, m_requestPath, kRequestIface, QStringLiteral("Response"), this,
                   SLOT(onResponse(uint,QVariantMap)));
    m_requestPath = actualPath;
    bus.connect(kPortalService, m_requestPath, kRequestIface, QStringLiteral("Response"), this,
                SLOT(onResponse(uint,QVariantMap)));
}

void PortalFileRequest::finish(const QList<QUrl> &urls) {
    if (std::exchange(m_settled, true))
        return;
    QDBusConnection::sessionBus().disconnect(kPortalService, m_requestPath, kRequestIface, QStringLiteral("Response"),
                                             this, SLOT(onResponse(uint,QVariantMap)));
    if (m_done)
        m_done(urls);
    deleteLater();
}

void PortalFileRequest::onResponse(uint response, const QVariantMap &results) {
    if (m_settled)
        return;
    QList<QUrl> urls;
    if (response == 0) {
        for (const QString &uri : results.value(QStringLiteral("uris")).toStringList()) {
            const QUrl url(uri);
            if (url.isValid())
                urls << url;
        }
    }
    finish(urls);
}

PortalFilePicker::PortalFilePicker(QObject *parent) : QObject(parent) {}

void PortalFilePicker::openFiles(QWindow *parent, std::function<void(const QList<QUrl> &)> done) {
    Q_UNUSED(parent);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QMetaObject::invokeMethod(this, [done = std::move(done)] {
            if (done)
                done({});
        }, Qt::QueuedConnection);
        return;
    }

    const QString token = QString("ask_%1_%2").arg(getpid()).arg(++m_counter);
    QString sender = bus.baseService();
    if (sender.startsWith(':'))
        sender.remove(0, 1);
    sender.replace('.', '_');
    const QString predictedPath = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    // Subscribe before calling OpenFile because the portal may emit Response before the reply arrives.
    auto *request = new PortalFileRequest(std::move(done), predictedPath, this);
    bus.connect(kPortalService, predictedPath, kRequestIface, QStringLiteral("Response"), request,
                SLOT(onResponse(uint,QVariantMap)));

    const QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("multiple"), true},
    };
    QDBusMessage call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kFileChooserIface,
                                                       QStringLiteral("OpenFile"));
    call.setArguments({QString(), tr("Attach Files"), options});

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), request);
    connect(watcher, &QDBusPendingCallWatcher::finished, request, [request](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (request->isSettled())
            return;
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            request->finish({});
            return;
        }
        request->rebindPath(reply.value().path());
    });
}
