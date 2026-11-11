// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kEventRetryMs = 5000;
constexpr qsizetype kMaxEventBuffer = 64 * 1024;

double wcagLuminance(const QColor &c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}

double contrastRatio(const QColor &a, const QColor &b) {
    const double l1 = wcagLuminance(a), l2 = wcagLuminance(b);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

QString blendHex(const QColor &fg, const QColor &bg, double fgWeight) {
    const double bgWeight = 1.0 - fgWeight;
    const int r = std::clamp(static_cast<int>(std::lround(fg.red() * fgWeight + bg.red() * bgWeight)), 0, 255);
    const int g = std::clamp(static_cast<int>(std::lround(fg.green() * fgWeight + bg.green() * bgWeight)), 0, 255);
    const int b = std::clamp(static_cast<int>(std::lround(fg.blue() * fgWeight + bg.blue() * bgWeight)), 0, 255);
    return QColor(r, g, b).name(QColor::HexRgb);
}

QString resolveMonoFont() {
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QString family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family().trimmed();
        if (!family.isEmpty())
            return family;
    }
    return QStringLiteral("monospace");
}
}

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory),
      m_monoFont(resolveMonoFont()),
      m_hyprctl(hyprctl) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, queried over its IPC socket
    // and refreshed on configreloaded events without spawning processes. An
    // explicit hyprctl path keeps process polling for isolated test rigs.
    if (m_hyprctl.isEmpty() && directory.isEmpty()) {
        const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
        const QByteArray signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!runtime.isEmpty() && !signature.isEmpty()) {
            const QString base = QString::fromLocal8Bit(runtime) + "/hypr/" + QString::fromLocal8Bit(signature);
            m_hyprSocket = base + "/.socket.sock";
            m_hyprEventSocket = base + "/.socket2.sock";
        }
    }

    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, this, [this] {
        if (m_roundingProcess.state() != QProcess::NotRunning)
            m_roundingProcess.kill();
        if (m_roundingSocket.state() != QLocalSocket::UnconnectedState)
            m_roundingSocket.abort();
    });

    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const QByteArray out = m_roundingProcess.readAllStandardOutput();
        if (code == 0 && status == QProcess::NormalExit)
            applyRoundingJson(out);
    });

    connect(&m_roundingSocket, &QLocalSocket::connected, this, [this] {
        m_roundingSocket.write("j/getoption decoration:rounding");
    });
    connect(&m_roundingSocket, &QLocalSocket::readyRead, this, [this] {
        m_roundingReply += m_roundingSocket.readAll();
    });
    connect(&m_roundingSocket, &QLocalSocket::errorOccurred, this, [this] {
        m_roundingTimeout.stop();
        m_roundingSocket.abort();
    });
    connect(&m_roundingSocket, &QLocalSocket::disconnected, this, [this] {
        m_roundingTimeout.stop();
        m_roundingReply += m_roundingSocket.readAll();
        applyRoundingJson(m_roundingReply);
        m_roundingReply.clear();
        if (m_roundingQueued) {
            m_roundingQueued = false;
            refreshRounding();
        }
    });

    m_eventRetry.setSingleShot(true);
    m_eventRetry.setInterval(kEventRetryMs);
    connect(&m_eventRetry, &QTimer::timeout, this, &Theme::connectEventSocket);
    connect(&m_eventSocket, &QLocalSocket::connected, this, [this] { m_eventRetried = false; });
    connect(&m_eventSocket, &QLocalSocket::readyRead, this, [this] {
        m_eventBuffer += m_eventSocket.readAll();
        if (m_eventBuffer.size() > kMaxEventBuffer)
            m_eventBuffer.clear();
        qsizetype end;
        bool reloaded = false;
        while ((end = m_eventBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_eventBuffer.left(end);
            m_eventBuffer.remove(0, end + 1);
            if (line.startsWith("configreloaded>>"))
                reloaded = true;
        }
        if (reloaded)
            refreshRounding();
    });
    connect(&m_eventSocket, &QLocalSocket::disconnected, this, &Theme::scheduleEventRetry);
    connect(&m_eventSocket, &QLocalSocket::errorOccurred, this, [this] {
        m_eventSocket.abort();
        scheduleEventRetry();
    });

    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    else if (!m_hyprEventSocket.isEmpty())
        connectEventSocket();

    reload();
}

Theme::~Theme() {
    m_roundingPoll.stop();
    m_roundingTimeout.stop();
    m_eventRetry.stop();
    m_roundingSocket.disconnect(this);
    m_eventSocket.disconnect(this);
    if (m_roundingSocket.state() != QLocalSocket::UnconnectedState)
        m_roundingSocket.abort();
    if (m_eventSocket.state() != QLocalSocket::UnconnectedState)
        m_eventSocket.abort();
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::applyRoundingJson(const QByteArray &json) {
    const auto value = QJsonDocument::fromJson(json).object().value("int");
    if (!value.isDouble() || value.toInt(-1) < 0)
        return;
    if (value.toInt() != m_radius) {
        m_radius = value.toInt();
        emit changed();
    }
}

void Theme::connectEventSocket() {
    if (m_hyprEventSocket.isEmpty() || m_eventSocket.state() != QLocalSocket::UnconnectedState)
        return;
    m_eventBuffer.clear();
    m_eventSocket.connectToServer(m_hyprEventSocket);
}

void Theme::scheduleEventRetry() {
    if (m_hyprEventSocket.isEmpty() || m_eventRetried || m_eventRetry.isActive())
        return;
    m_eventRetried = true;
    m_eventRetry.start();
}

void Theme::refreshRounding() {
    if (!m_hyprctl.isEmpty()) {
        if (m_roundingProcess.state() != QProcess::NotRunning)
            return;
        m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
        m_roundingTimeout.start();
        return;
    }
    if (m_hyprSocket.isEmpty())
        return;
    if (m_roundingSocket.state() != QLocalSocket::UnconnectedState) {
        m_roundingQueued = true;
        return;
    }
    m_roundingReply.clear();
    m_roundingSocket.connectToServer(m_hyprSocket);
    m_roundingTimeout.start();
}

Theme::Colors Theme::readColors(const QString &path) {
    Colors c;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return c;

    static const QRegularExpression kvPattern(
        QStringLiteral(R"re(^\s*([a-zA-Z0-9_]+)\s*=\s*(?:"([^"]*)"|'([^']*)')\s*(?:#.*)?$)re"));
    static const QRegularExpression hexPattern(QStringLiteral("^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));

    QHash<QString, QString> raw;
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (const QString &line : lines) {
        const auto match = kvPattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString val = !match.captured(2).isNull() ? match.captured(2).trimmed() : match.captured(3).trimmed();
        raw.insert(match.captured(1), val);
    }

    auto colorValue = [&raw](const char *key) -> QString {
        const QString val = raw.value(QLatin1String(key));
        return hexPattern.match(val).hasMatch() ? val : QString();
    };

    const QString accent = colorValue("accent");
    if (!accent.isEmpty())
        c.accent = accent;

    const QString mode = raw.value(QStringLiteral("mode")).toLower();
    const QString bg = colorValue("background");
    if (mode == QLatin1String("light"))
        c.light = true;
    else if (mode == QLatin1String("dark"))
        c.light = false;
    else if (!bg.isEmpty())
        c.light = wcagLuminance(QColor(bg)) > 0.5;

    const QString darkBg = colorValue("dark_background");
    if (!darkBg.isEmpty())
        c.surface = darkBg;
    else if (!bg.isEmpty())
        c.surface = bg;
    else
        c.surface = c.light ? QStringLiteral("#f5f5f7") : QStringLiteral("#161618");

    const QString lighterBg = colorValue("lighter_background");
    if (c.light) {
        if (!bg.isEmpty())
            c.popover = bg;
        else if (!lighterBg.isEmpty())
            c.popover = lighterBg;
        else
            c.popover = QStringLiteral("#ffffff");
    } else {
        if (!lighterBg.isEmpty())
            c.popover = lighterBg;
        else if (!bg.isEmpty())
            c.popover = bg;
        else
            c.popover = QStringLiteral("#202023");
    }

    const QString fg = colorValue("foreground");
    if (!fg.isEmpty())
        c.text = fg;
    else
        c.text = c.light ? QStringLiteral("#1c1c1e") : QStringLiteral("#ececee");

    const QColor surfaceColor(c.surface);
    const QColor textColor(c.text);

    const QString lightFg = colorValue("light_foreground");
    if (!lightFg.isEmpty() && contrastRatio(QColor(lightFg), surfaceColor) >= 3.5)
        c.dim = lightFg;
    else if (!fg.isEmpty() || !bg.isEmpty() || !darkBg.isEmpty())
        c.dim = blendHex(textColor, surfaceColor, 0.70);
    else
        c.dim = c.light ? QStringLiteral("#5c5c66") : QStringLiteral("#9a9aa2");

    const QString darkFg = colorValue("dark_foreground");
    const QString muted = colorValue("muted");
    if (!darkFg.isEmpty() && contrastRatio(QColor(darkFg), surfaceColor) >= 2.2)
        c.faint = darkFg;
    else if (!muted.isEmpty() && contrastRatio(QColor(muted), surfaceColor) >= 2.2)
        c.faint = muted;
    else if (!darkFg.isEmpty() || !muted.isEmpty() || !fg.isEmpty() || !bg.isEmpty() || !darkBg.isEmpty())
        c.faint = blendHex(textColor, surfaceColor, 0.48);
    else
        c.faint = c.light ? QStringLiteral("#8e8e93") : QStringLiteral("#64646b");

    const QString red = colorValue("red");
    if (!red.isEmpty())
        c.danger = red;
    else
        c.danger = c.light ? QStringLiteral("#d93025") : QStringLiteral("#ff6b6b");

    c.border = c.light ? QStringLiteral("#22000000") : QStringLiteral("#26ffffff");
    c.divider = c.light ? QStringLiteral("#14000000") : QStringLiteral("#14ffffff");
    c.hover = c.light ? QStringLiteral("#0d000000") : QStringLiteral("#0fffffff");
    c.keycap = c.light ? QStringLiteral("#12000000") : QStringLiteral("#1affffff");
    c.shadow = c.light ? QStringLiteral("#26000000") : QStringLiteral("#66000000");
    c.scrim = c.light ? QStringLiteral("#55000000") : QStringLiteral("#99000000");

    const QString sel = colorValue("selection");
    if (!sel.isEmpty()) {
        QColor selColor(sel);
        selColor.setAlpha(contrastRatio(selColor, textColor) >= 2.0 ? 115 : 46);
        c.selected = selColor.name(QColor::HexArgb);
    } else {
        c.selected = c.light ? QStringLiteral("#14000000") : QStringLiteral("#1affffff");
    }

    const QString darkerBg = colorValue("darker_background");
    if (!darkerBg.isEmpty())
        c.codeBackground = darkerBg;
    else
        c.codeBackground = c.light ? QStringLiteral("#0a000000") : QStringLiteral("#0dffffff");

    return c;
}

QString Theme::readAccent(const QString &path) { return readColors(path).accent; }

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    return wcagLuminance(QColor(m_colors.accent)) > .179 ? QStringLiteral("black") : QStringLiteral("white");
}

void Theme::reload() {
    refreshRounding();
    if (!m_hyprEventSocket.isEmpty() && m_eventSocket.state() == QLocalSocket::UnconnectedState && !m_eventRetry.isActive())
        connectEventSocket();

    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const Colors colors = readColors(m_directory + "/theme/colors.toml");
    const QString mono = resolveMonoFont();
    if (colors != m_colors || mono != m_monoFont) {
        m_colors = colors;
        m_monoFont = mono;
        emit changed();
    }
}
