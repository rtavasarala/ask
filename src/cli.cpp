#include "cli.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <poll.h>
#include <unistd.h>

#include "attachment.h"
#include "commands.h"
#include "context.h"
#include "prompt.h"
#include "settings.h"

namespace {
constexpr int kMaxStdinBytes = 256 * 1024;

bool takeOptionValue(const QStringList &args, int &i, const QString &name, QString &out, QString &error) {
    const QString &current = args.at(i);
    const QString prefix = name + QLatin1Char('=');
    if (current.startsWith(prefix)) {
        out = current.mid(prefix.size()).trimmed();
        if (out.isEmpty()) {
            error = QStringLiteral("Option %1 requires a non-empty value.").arg(name);
            return false;
        }
        return true;
    }
    if (i + 1 >= args.size()) {
        error = QStringLiteral("Option %1 requires a value.").arg(name);
        return false;
    }
    out = args.at(++i).trimmed();
    if (out.isEmpty()) {
        error = QStringLiteral("Option %1 requires a non-empty value.").arg(name);
        return false;
    }
    return true;
}

QString readPipedStdin(bool nonBlockingProbe) {
    if (isatty(STDIN_FILENO))
        return {};
    if (nonBlockingProbe) {
        struct pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 50) <= 0)
            return {};
    }
    QByteArray buffer;
    char chunk[4096];
    while (buffer.size() < kMaxStdinBytes) {
        const ssize_t n = ::read(STDIN_FILENO, chunk,
                                 size_t(qMin<qint64>(sizeof(chunk), kMaxStdinBytes - buffer.size())));
        if (n > 0) {
            buffer.append(chunk, int(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    QStringDecoder decoder(QStringConverter::Utf8);
    return decoder(buffer);
}

QString conciseStderr(int code, const QByteArray &stderrBytes) {
    QStringList lines;
    for (const QString &line : QString::fromUtf8(stderrBytes).split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1String("[notice]")))
            lines << trimmed;
    }
    QString text = lines.mid(qMax(0, lines.size() - 4)).join('\n');
    if (text.size() > 500)
        text = QStringLiteral("…") + text.right(499);
    return text.isEmpty() ? QStringLiteral("fx exited with code %1").arg(code) : text;
}

int emitQueryError(bool jsonMode, const QString &message) {
    if (jsonMode) {
        const QByteArray payload = QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), message},
        }).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(payload.constData(), 1, size_t(payload.size()), stdout);
        std::fflush(stdout);
    } else {
        const QByteArray payload = (QStringLiteral("ask: ") + message + QLatin1Char('\n')).toUtf8();
        std::fwrite(payload.constData(), 1, size_t(payload.size()), stderr);
        std::fflush(stderr);
    }
    return 1;
}

std::optional<AiCommand> resolveCommand(const QString &id) {
    if (const AiCommand *builtin = findAiCommand(id))
        return *builtin;
    for (const AiCommand &custom : loadCustomCommands(defaultCommandsPath())) {
        if (custom.id == id)
            return custom;
    }
    return std::nullopt;
}
}

QString cliHelpText() {
    return QStringLiteral(
        "Usage:\n"
        "  ask                              Toggle the Ask popup window\n"
        "  ask --background                 Start resident instance without showing the window\n"
        "  ask open [options] [prompt...]   Open the popup with an optional prompt or command\n"
        "  ask query [options] [prompt...]  Run a headless query and stream Markdown to stdout\n"
        "  ask help | --help | -h           Show this help text\n"
        "  ask --version | -v               Print the version\n"
        "\n"
        "Options:\n"
        "  --new                            Start a fresh chat (GUI mode)\n"
        "  --command <id>                   Run a built-in or custom AI command\n"
        "  --model <id>                     Override the model for this request\n"
        "  --effort <level>                 Override reasoning effort (e.g. low, medium, high)\n"
        "  --fast                           Enable Fast mode when supported by the model\n"
        "  --selection                      Include the current primary selection (query mode)\n"
        "  --file <path>                    Attach an image or UTF-8 text file (repeatable)\n"
        "  --json                           Emit a single JSON object (query mode)\n"
        "  --no-save                        Do not persist the fx session (query mode)\n");
}

ParsedCli parseCli(int argc, char *const argv[]) {
    ParsedCli out;
    if (argc <= 1 || !argv)
        return out;

    QStringList args;
    args.reserve(argc - 1);
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    const QString first = args.first();
    if (first == QLatin1String("help") || first == QLatin1String("--help") || first == QLatin1String("-h")) {
        out.mode = ParsedCli::PrintHelp;
        return out;
    }
    if (first == QLatin1String("version") || first == QLatin1String("--version") || first == QLatin1String("-v")) {
        out.mode = ParsedCli::PrintVersion;
        return out;
    }
    if (args.size() == 1 && first == QLatin1String("--background")) {
        out.mode = ParsedCli::BackgroundGui;
        return out;
    }

    int index = 0;
    if (first == QLatin1String("query")) {
        out.mode = ParsedCli::QueryHeadless;
        index = 1;
    } else if (first == QLatin1String("open")) {
        out.mode = ParsedCli::OpenGui;
        index = 1;
    } else if (first == QLatin1String("--new") || first == QLatin1String("-n")
               || first == QLatin1String("--command") || first.startsWith(QLatin1String("--command="))
               || first == QLatin1String("--file") || first.startsWith(QLatin1String("--file="))) {
        out.mode = ParsedCli::OpenGui;
        index = 0;
    } else if (first.startsWith(QLatin1Char('-'))) {
        out.mode = ParsedCli::Error;
        out.error = QStringLiteral("Unknown option: %1").arg(first);
        return out;
    } else {
        out.mode = ParsedCli::Error;
        out.error = QStringLiteral("Unknown command: %1").arg(first);
        return out;
    }

    QStringList positional;
    bool afterDoubleDash = false;
    for (int i = index; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (afterDoubleDash) {
            positional << arg;
            continue;
        }
        if (arg == QLatin1String("--")) {
            afterDoubleDash = true;
            continue;
        }
        if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            out.mode = ParsedCli::PrintHelp;
            return out;
        }
        if (arg == QLatin1String("--version") || arg == QLatin1String("-v")) {
            out.mode = ParsedCli::PrintVersion;
            return out;
        }
        if (arg == QLatin1String("--model") || arg.startsWith(QLatin1String("--model="))) {
            if (!takeOptionValue(args, i, QStringLiteral("--model"), out.model, out.error)) {
                out.mode = ParsedCli::Error;
                return out;
            }
            continue;
        }
        if (arg == QLatin1String("--effort") || arg.startsWith(QLatin1String("--effort="))) {
            if (!takeOptionValue(args, i, QStringLiteral("--effort"), out.effort, out.error)) {
                out.mode = ParsedCli::Error;
                return out;
            }
            continue;
        }
        if (arg == QLatin1String("--command") || arg.startsWith(QLatin1String("--command="))) {
            if (!takeOptionValue(args, i, QStringLiteral("--command"), out.commandId, out.error)) {
                out.mode = ParsedCli::Error;
                return out;
            }
            continue;
        }
        if (arg == QLatin1String("--file") || arg.startsWith(QLatin1String("--file="))) {
            QString file;
            if (!takeOptionValue(args, i, QStringLiteral("--file"), file, out.error)) {
                out.mode = ParsedCli::Error;
                return out;
            }
            out.files << file;
            continue;
        }
        if (arg == QLatin1String("--fast")) {
            out.fast = true;
            continue;
        }
        if (arg == QLatin1String("--selection")) {
            out.withSelection = true;
            continue;
        }
        if (out.mode == ParsedCli::OpenGui && (arg == QLatin1String("--new") || arg == QLatin1String("-n"))) {
            out.newChat = true;
            continue;
        }
        if (out.mode == ParsedCli::QueryHeadless && arg == QLatin1String("--json")) {
            out.json = true;
            continue;
        }
        if (out.mode == ParsedCli::QueryHeadless && arg == QLatin1String("--no-save")) {
            out.noSave = true;
            continue;
        }
        if (arg.startsWith(QLatin1Char('-'))) {
            out.mode = ParsedCli::Error;
            out.error = QStringLiteral("Unknown option: %1").arg(arg);
            return out;
        }
        positional << arg;
    }

    out.prompt = positional.join(QLatin1Char(' ')).trimmed();
    return out;
}

int runHeadlessQuery(const ParsedCli &cli, int argc, char *argv[], const QString &fxPath) {
    const bool hadApp = QCoreApplication::instance() != nullptr;
    std::optional<QCoreApplication> ownedApp;
    if (!hadApp)
        ownedApp.emplace(argc, argv);

    std::optional<AiCommand> command;
    if (!cli.commandId.isEmpty()) {
        command = resolveCommand(cli.commandId);
        if (!command)
            return emitQueryError(cli.json, QStringLiteral("Unknown command: %1").arg(cli.commandId));
    }

    const QString stdinText = cli.prompt.isEmpty() ? readPipedStdin(hadApp) : QString();

    QList<Attachment> attachments;
    const bool wantsSelection = cli.withSelection || (command && command->requiresSelection);
    if (wantsSelection) {
        const Context ctx = Context::gather();
        if (!ctx.selection.trimmed().isEmpty()) {
            Attachment sel;
            sel.kind = Attachment::Selection;
            sel.app = appDisplayName(ctx.appClass);
            sel.payload = ctx.selection;
            sel.enabled = true;
            attachments << sel;
        } else if (command && !stdinText.trimmed().isEmpty()) {
            Attachment sel;
            sel.kind = Attachment::Selection;
            sel.payload = stdinText.left(Context::kMaxSelection);
            sel.enabled = true;
            attachments << sel;
        }
        if (command && command->requiresSelection && attachments.isEmpty())
            return emitQueryError(cli.json, QStringLiteral("Command '%1' requires selected text or piped input.")
                                                .arg(cli.commandId));
    }

    for (const QString &filePath : cli.files) {
        QString fileError;
        const auto att = attachmentFromLocalFile(filePath, &fileError);
        if (!att)
            return emitQueryError(cli.json, fileError);
        attachments << *att;
    }

    QString question;
    if (command) {
        question = cli.prompt.isEmpty()
            ? command->promptTemplate
            : command->promptTemplate + QStringLiteral("\n\n") + cli.prompt;
    } else {
        question = !cli.prompt.isEmpty() ? cli.prompt : stdinText.trimmed();
    }
    if (question.trimmed().isEmpty())
        return emitQueryError(cli.json, QStringLiteral("No prompt provided."));

    const QString fx = fxPath.isEmpty() ? resolveFxExecutable() : fxPath;
    if (fx.isEmpty()) {
        return emitQueryError(cli.json,
                              QStringLiteral("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
    }

    QTemporaryDir tempWorkspace;
    if (!tempWorkspace.isValid())
        return emitQueryError(cli.json, QStringLiteral("Could not create temporary workspace directory."));

    const AssembledPrompt assembled = assemblePrompt(question, attachments);
    QStringList args{QStringLiteral("ask"), QStringLiteral("--no-color"), QStringLiteral("--system"),
                     QString::fromUtf8(askSystemPrompt())};
    if (!cli.model.isEmpty())
        args << QStringLiteral("--model") << cli.model;
    if (!cli.effort.isEmpty())
        args << QStringLiteral("--effort") << cli.effort;
    if (cli.fast)
        args << QStringLiteral("--fast");
    if (cli.noSave)
        args << QStringLiteral("--no-save");
    for (const QString &image : assembled.images)
        args << QStringLiteral("--image") << image;
    args << QStringLiteral("--") << assembled.text;

    QProcess process;
    process.setWorkingDirectory(tempWorkspace.path());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);

    QStringDecoder decoder(QStringConverter::Utf8);
    QString answer;
    QByteArray stderrBytes;
    bool failedToStart = false;
    int exitCode = 1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QEventLoop loop;

    auto consumeStdout = [&] {
        const QByteArray bytes = process.readAllStandardOutput();
        if (bytes.isEmpty())
            return;
        const QString chunk = decoder(bytes);
        answer += chunk;
        if (!cli.json) {
            const QByteArray utf8 = chunk.toUtf8();
            std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stdout);
            std::fflush(stdout);
        }
    };

    QObject::connect(&process, &QProcess::readyReadStandardOutput, &loop, consumeStdout);
    QObject::connect(&process, &QProcess::readyReadStandardError, &loop, [&] {
        stderrBytes += process.readAllStandardError();
    });
    QObject::connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            failedToStart = true;
            loop.quit();
        }
    });
    QObject::connect(&process, &QProcess::finished, &loop, [&](int code, QProcess::ExitStatus status) {
        consumeStdout();
        stderrBytes += process.readAllStandardError();
        exitCode = code;
        exitStatus = status;
        loop.quit();
    });

    process.start(fx, args, QIODevice::ReadOnly);
    if (process.state() != QProcess::NotRunning && !failedToStart)
        loop.exec();
    else if (!failedToStart) {
        consumeStdout();
        stderrBytes += process.readAllStandardError();
        exitCode = process.exitCode();
        exitStatus = process.exitStatus();
    }

    if (failedToStart) {
        return emitQueryError(cli.json,
                              QStringLiteral("fx is not installed or not on PATH. Install it from https://fx.sh and try again."));
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0)
        return emitQueryError(cli.json, conciseStderr(exitCode, stderrBytes));

    if (cli.json) {
        const QByteArray payload = QJsonDocument(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("answer"), answer},
            {QStringLiteral("model"), cli.model},
        }).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(payload.constData(), 1, size_t(payload.size()), stdout);
        std::fflush(stdout);
    } else if (!answer.isEmpty() && !answer.endsWith(QLatin1Char('\n')) && isatty(STDOUT_FILENO)) {
        std::fwrite("\n", 1, 1, stdout);
        std::fflush(stdout);
    }
    return 0;
}
