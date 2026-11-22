#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>

// Raycast-style AI Commands. Built-in titles are English source text that QML
// translates; custom commands carry a user-authored customTitle.
struct AiCommand {
    QString id;
    const char *title = nullptr;
    QString promptTemplate;
    bool requiresSelection = true;
    QString customTitle;
};

const QList<AiCommand> &aiCommands();
const AiCommand *findAiCommand(const QString &id);
QList<AiCommand> loadCustomCommands(const QString &path);
QString defaultCommandsPath();
QString translateTargetLanguageName(const QString &languageCode);
