QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release qtquickcompiler
TARGET = ask
TEMPLATE = app

HEADERS += \
    src/attachment.h \
    src/attachmentsmodel.h \
    src/commands.h \
    src/context.h \
    src/prompt.h \
    src/settings.h \
    src/backend.h \
    src/instance.h \
    src/messagesmodel.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/attachment.cpp \
    src/attachmentsmodel.cpp \
    src/commands.cpp \
    src/context.cpp \
    src/prompt.cpp \
    src/settings.cpp \
    src/main.cpp \
    src/backend.cpp \
    src/instance.cpp \
    src/messagesmodel.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
