QT += core gui qml quick quickcontrols2 network
CONFIG += c++17
TARGET = ask_screenshots
TEMPLATE = app

INCLUDEPATH += ../../src
DEFINES += SHOTS_DIR=\\\"$$PWD\\\"

HEADERS += $$files(../../src/*.h)
SOURCES += main.cpp \
    ../../src/attachment.cpp ../../src/attachmentsmodel.cpp ../../src/backend.cpp ../../src/commands.cpp \
    ../../src/context.cpp ../../src/instance.cpp ../../src/messagesmodel.cpp ../../src/prompt.cpp \
    ../../src/settings.cpp ../../src/theme.cpp ../../src/translator.cpp
RESOURCES += ../../src/resources.qrc
