#include "commands.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
QString slugifyTitle(const QString &title) {
    QString slug = title.toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    slug.replace(nonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-')))
        slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug;
}
}  // namespace

const QList<AiCommand> &aiCommands() {
    static const QList<AiCommand> commands{
        {"summarize", QT_TR_NOOP("Summarize"), "Summarize the selected text.", true, {}},
        {"explain", QT_TR_NOOP("Explain This"), "Explain the selected text in simple terms.", true, {}},
        {"fixSpelling", QT_TR_NOOP("Fix Spelling and Grammar"),
         "Fix the spelling and grammar of the selected text. Reply with only the corrected text.", true, {}},
        {"improveWriting", QT_TR_NOOP("Improve Writing"),
         "Improve the writing of the selected text while keeping its meaning and tone. Reply with only the improved text.", true, {}},
        {"makeShorter", QT_TR_NOOP("Make Shorter"),
         "Rewrite the selected text to be shorter while keeping its meaning. Reply with only the shortened text.", true, {}},
        {"translate", QT_TR_NOOP("Translate to English"),
         "Translate the selected text to English. Reply with only the translation.", true, {}},
        {"explainCode", QT_TR_NOOP("Explain Code"), "Explain what the selected code does, step by step.", true, {}},
    };
    return commands;
}

const AiCommand *findAiCommand(const QString &id) {
    for (const AiCommand &command : aiCommands())
        if (command.id == id)
            return &command;
    return nullptr;
}

QString defaultCommandsPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/ask/commands.json");
}

QList<AiCommand> loadCustomCommands(const QString &path) {
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        return {};

    QJsonArray entries;
    if (doc.isArray()) {
        entries = doc.array();
    } else if (doc.isObject()) {
        const QJsonValue list = doc.object().value(QLatin1String("commands"));
        if (!list.isArray())
            return {};
        entries = list.toArray();
    } else {
        return {};
    }

    QList<AiCommand> result;
    QSet<QString> usedIds;
    for (const AiCommand &builtin : aiCommands())
        usedIds.insert(builtin.id);

    int fallbackIndex = 0;
    for (const QJsonValue &entry : entries) {
        if (!entry.isObject())
            continue;
        const QJsonObject obj = entry.toObject();
        const QString title = obj.value(QLatin1String("title")).toString().trimmed();
        if (title.isEmpty())
            continue;

        QString prompt = obj.value(QLatin1String("prompt")).toString().trimmed();
        if (prompt.isEmpty())
            prompt = obj.value(QLatin1String("promptTemplate")).toString().trimmed();
        if (prompt.isEmpty())
            continue;

        const QJsonValue reqVal = obj.value(QLatin1String("requiresSelection"));
        const bool requiresSelection = reqVal.isBool() ? reqVal.toBool() : true;

        QString id = obj.value(QLatin1String("id")).toString().trimmed();
        if (id.isEmpty()) {
            QString slug = slugifyTitle(title);
            if (slug.isEmpty())
                slug = QString::number(++fallbackIndex);
            id = QStringLiteral("custom:") + slug;
        }
        if (usedIds.contains(id)) {
            const QString base = id;
            int suffix = 2;
            while (usedIds.contains(base + QLatin1Char('-') + QString::number(suffix)))
                ++suffix;
            id = base + QLatin1Char('-') + QString::number(suffix);
        }
        usedIds.insert(id);

        AiCommand cmd;
        cmd.id = id;
        cmd.title = nullptr;
        cmd.promptTemplate = prompt;
        cmd.requiresSelection = requiresSelection;
        cmd.customTitle = title;
        result.append(cmd);
    }

    return result;
}

QString translateTargetLanguageName(const QString &languageCode) {
    QString code = languageCode.trimmed();
    if (code.isEmpty())
        return {};
    const int dot = code.indexOf(QLatin1Char('.'));
    if (dot >= 0)
        code.truncate(dot);
    const int at = code.indexOf(QLatin1Char('@'));
    if (at >= 0)
        code.truncate(at);
    code.replace(QLatin1Char('_'), QLatin1Char('-'));

    const QStringList parts = code.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    const QString base = parts.at(0).toLower();
    const QString normalized = parts.size() >= 2
                                   ? base + QLatin1Char('-') + parts.at(1).toUpper()
                                   : base;

    static const QHash<QString, QString> names{
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("da"), QStringLiteral("Danish")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("es-MX"), QStringLiteral("Spanish")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese")},
        {QStringLiteral("pt-BR"), QStringLiteral("Brazilian Portuguese")},
        {QStringLiteral("nl"), QStringLiteral("Dutch")},
        {QStringLiteral("sv"), QStringLiteral("Swedish")},
        {QStringLiteral("no"), QStringLiteral("Norwegian")},
        {QStringLiteral("nb"), QStringLiteral("Norwegian")},
        {QStringLiteral("nn"), QStringLiteral("Norwegian")},
        {QStringLiteral("fi"), QStringLiteral("Finnish")},
        {QStringLiteral("is"), QStringLiteral("Icelandic")},
        {QStringLiteral("pl"), QStringLiteral("Polish")},
        {QStringLiteral("cs"), QStringLiteral("Czech")},
        {QStringLiteral("sk"), QStringLiteral("Slovak")},
        {QStringLiteral("sl"), QStringLiteral("Slovenian")},
        {QStringLiteral("hr"), QStringLiteral("Croatian")},
        {QStringLiteral("sr"), QStringLiteral("Serbian")},
        {QStringLiteral("ro"), QStringLiteral("Romanian")},
        {QStringLiteral("bg"), QStringLiteral("Bulgarian")},
        {QStringLiteral("hu"), QStringLiteral("Hungarian")},
        {QStringLiteral("el"), QStringLiteral("Greek")},
        {QStringLiteral("lt"), QStringLiteral("Lithuanian")},
        {QStringLiteral("lv"), QStringLiteral("Latvian")},
        {QStringLiteral("et"), QStringLiteral("Estonian")},
        {QStringLiteral("ga"), QStringLiteral("Irish")},
        {QStringLiteral("ca"), QStringLiteral("Catalan")},
        {QStringLiteral("uk"), QStringLiteral("Ukrainian")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("ja"), QStringLiteral("Japanese")},
        {QStringLiteral("ko"), QStringLiteral("Korean")},
        {QStringLiteral("zh"), QStringLiteral("Chinese")},
        {QStringLiteral("zh-CN"), QStringLiteral("Simplified Chinese")},
        {QStringLiteral("zh-TW"), QStringLiteral("Traditional Chinese")},
        {QStringLiteral("zh-HK"), QStringLiteral("Traditional Chinese")},
        {QStringLiteral("ar"), QStringLiteral("Arabic")},
        {QStringLiteral("he"), QStringLiteral("Hebrew")},
        {QStringLiteral("fa"), QStringLiteral("Persian")},
        {QStringLiteral("hi"), QStringLiteral("Hindi")},
        {QStringLiteral("bn"), QStringLiteral("Bengali")},
        {QStringLiteral("si"), QStringLiteral("Sinhala")},
        {QStringLiteral("tr"), QStringLiteral("Turkish")},
        {QStringLiteral("vi"), QStringLiteral("Vietnamese")},
        {QStringLiteral("th"), QStringLiteral("Thai")},
        {QStringLiteral("id"), QStringLiteral("Indonesian")},
        {QStringLiteral("ms"), QStringLiteral("Malay")},
        {QStringLiteral("fil"), QStringLiteral("Filipino")},
    };

    if (const auto it = names.constFind(normalized); it != names.cend())
        return *it;
    if (const auto it = names.constFind(base); it != names.cend())
        return *it;

    const QLocale::Language lang = QLocale(normalized).language();
    if (lang == QLocale::C || lang == QLocale::AnyLanguage)
        return {};
    return QLocale::languageToString(lang);
}
