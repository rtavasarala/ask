QT += core gui qml quick quickcontrols2 network dbus
CONFIG += c++17
TARGET = ask_screenshots
TEMPLATE = app

exists(/usr/include/LayerShellQt/Window) {
    DEFINES += HAVE_LAYER_SHELL
    LIBS += -lLayerShellQtInterface
}

INCLUDEPATH += ../../src
DEFINES += SHOTS_DIR=\\\"$$PWD\\\"

HEADERS += $$files(../../src/*.h)
SOURCES += main.cpp \
    ../../src/attachment.cpp ../../src/attachmentsmodel.cpp ../../src/backend.cpp ../../src/cli.cpp \
    ../../src/commands.cpp ../../src/context.cpp ../../src/history.cpp ../../src/instance.cpp \
    ../../src/markdown.cpp ../../src/messagesmodel.cpp ../../src/overlay.cpp \
    ../../src/portalfilepicker.cpp ../../src/prompt.cpp ../../src/settings.cpp \
    ../../src/theme.cpp ../../src/translator.cpp
RESOURCES += ../../src/resources.qrc
