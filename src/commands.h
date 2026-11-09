#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>

// Raycast-style AI Commands. Titles are English source text that QML passes
// through qsTr; the templates are instructions to the model and stay English.
struct AiCommand {
    QString id;
    const char *title;
    QString promptTemplate;
    bool requiresSelection;
};

const QList<AiCommand> &aiCommands();
const AiCommand *findAiCommand(const QString &id);
