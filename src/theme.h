#pragma once

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(bool light READ light NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString popover READ popover NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString dim READ dim NOTIFY changed)
    Q_PROPERTY(QString faint READ faint NOTIFY changed)
    Q_PROPERTY(QString danger READ danger NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString divider READ divider NOTIFY changed)
    Q_PROPERTY(QString hover READ hover NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(QString keycap READ keycap NOTIFY changed)
    Q_PROPERTY(QString codeBackground READ codeBackground NOTIFY changed)
    Q_PROPERTY(QString shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QString scrim READ scrim NOTIFY changed)
    Q_PROPERTY(QString monoFont READ monoFont NOTIFY changed)
    Q_PROPERTY(QString mono READ monoFont NOTIFY changed)

public:
    struct Colors {
        bool light = false;
        QString accent = "#FFD60A";
        QString surface = "#161618";
        QString popover = "#202023";
        QString text = "#ececee";
        QString dim = "#9a9aa2";
        QString faint = "#64646b";
        QString danger = "#ff6b6b";
        QString border = "#26ffffff";
        QString divider = "#14ffffff";
        QString hover = "#0fffffff";
        QString selected = "#1affffff";
        QString keycap = "#1affffff";
        QString codeBackground = "#0dffffff";
        QString shadow = "#66000000";
        QString scrim = "#99000000";

        bool operator==(const Colors &other) const {
            return light == other.light && accent == other.accent && surface == other.surface &&
                   popover == other.popover && text == other.text && dim == other.dim &&
                   faint == other.faint && danger == other.danger && border == other.border &&
                   divider == other.divider && hover == other.hover && selected == other.selected &&
                   keycap == other.keycap && codeBackground == other.codeBackground &&
                   shadow == other.shadow && scrim == other.scrim;
        }
        bool operator!=(const Colors &other) const { return !(*this == other); }
    };

    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_colors.accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    bool light() const { return m_colors.light; }
    QString surface() const { return m_colors.surface; }
    QString popover() const { return m_colors.popover; }
    QString text() const { return m_colors.text; }
    QString dim() const { return m_colors.dim; }
    QString faint() const { return m_colors.faint; }
    QString danger() const { return m_colors.danger; }
    QString border() const { return m_colors.border; }
    QString divider() const { return m_colors.divider; }
    QString hover() const { return m_colors.hover; }
    QString selected() const { return m_colors.selected; }
    QString keycap() const { return m_colors.keycap; }
    QString codeBackground() const { return m_colors.codeBackground; }
    QString shadow() const { return m_colors.shadow; }
    QString scrim() const { return m_colors.scrim; }
    QString monoFont() const { return m_monoFont; }
    const Colors &colors() const { return m_colors; }

    static Colors readColors(const QString &path);
    static QString readAccent(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();
    void applyRoundingJson(const QByteArray &json);
    void connectEventSocket();
    void scheduleEventRetry();

    QString m_directory;
    Colors m_colors;
    QString m_monoFont;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;

    QString m_hyprctl;
    QString m_hyprSocket;
    QString m_hyprEventSocket;
    int m_radius = 0;

    QProcess m_roundingProcess;
    QLocalSocket m_roundingSocket;
    QByteArray m_roundingReply;
    bool m_roundingQueued = false;
    QTimer m_roundingPoll, m_roundingTimeout;

    QLocalSocket m_eventSocket;
    QByteArray m_eventBuffer;
    QTimer m_eventRetry;
    bool m_eventRetried = false;
};
