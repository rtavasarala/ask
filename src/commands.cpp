#include "commands.h"

const QList<AiCommand> &aiCommands() {
    static const QList<AiCommand> commands{
        {"summarize", QT_TR_NOOP("Summarize"), "Summarize the selected text.", true},
        {"explain", QT_TR_NOOP("Explain This"), "Explain the selected text in simple terms.", true},
        {"fixSpelling", QT_TR_NOOP("Fix Spelling and Grammar"),
         "Fix the spelling and grammar of the selected text. Reply with only the corrected text.", true},
        {"improveWriting", QT_TR_NOOP("Improve Writing"),
         "Improve the writing of the selected text while keeping its meaning and tone. Reply with only the improved text.", true},
        {"makeShorter", QT_TR_NOOP("Make Shorter"),
         "Rewrite the selected text to be shorter while keeping its meaning. Reply with only the shortened text.", true},
        {"translate", QT_TR_NOOP("Translate to English"),
         "Translate the selected text to English. Reply with only the translation.", true},
        {"explainCode", QT_TR_NOOP("Explain Code"), "Explain what the selected code does, step by step.", true},
    };
    return commands;
}

const AiCommand *findAiCommand(const QString &id) {
    for (const AiCommand &command : aiCommands())
        if (command.id == id)
            return &command;
    return nullptr;
}
