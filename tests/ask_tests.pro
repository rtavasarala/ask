QT += core gui quick quickcontrols2 testlib network dbus
CONFIG += c++17 testcase
TARGET = ask_tests
TEMPLATE = app

exists(/usr/include/LayerShellQt/Window) {
    DEFINES += HAVE_LAYER_SHELL
    LIBS += -lLayerShellQtInterface
}

INCLUDEPATH += ../src
DEFINES += SRC_DIR=\\\"$$PWD/../src\\\" TESTS_DIR=\\\"$$PWD\\\"
RESOURCES += ../src/resources.qrc

HEADERS += \
    ../src/attachment.h \
    ../src/attachmentsmodel.h \
    ../src/backend.h \
    ../src/cli.h \
    ../src/commands.h \
    ../src/context.h \
    ../src/filepicker.h \
    ../src/history.h \
    ../src/instance.h \
    ../src/markdown.h \
    ../src/messagesmodel.h \
    ../src/overlay.h \
    ../src/portalfilepicker.h \
    ../src/prompt.h \
    ../src/settings.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    ask_tests.cpp \
    ../src/attachment.cpp \
    ../src/attachmentsmodel.cpp \
    ../src/backend.cpp \
    ../src/cli.cpp \
    ../src/commands.cpp \
    ../src/context.cpp \
    ../src/history.cpp \
    ../src/instance.cpp \
    ../src/markdown.cpp \
    ../src/messagesmodel.cpp \
    ../src/overlay.cpp \
    ../src/portalfilepicker.cpp \
    ../src/prompt.cpp \
    ../src/settings.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
