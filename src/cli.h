#pragma once

#include <QString>
#include <QStringList>

struct ParsedCli {
    enum Mode { ToggleGui, BackgroundGui, OpenGui, QueryHeadless, PrintHelp, PrintVersion, Error };
    Mode mode = ToggleGui;
    QString prompt;
    QString commandId;
    QString model;
    QString effort;
    bool fast = false;
    bool newChat = false;
    bool withSelection = false;
    bool json = false;
    bool noSave = false;
    QStringList files;
    QString error;
};

// Parses argv before QGuiApplication exists so help, version, and headless
// queries never pay for Wayland or QML initialization.
ParsedCli parseCli(int argc, char *const argv[]);
int runHeadlessQuery(const ParsedCli &cli, int argc, char *argv[], const QString &fxPath = {});
QString cliHelpText();
