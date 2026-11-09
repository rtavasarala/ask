QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release qtquickcompiler
TARGET = ask
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/instance.h \
    src/messagesmodel.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/instance.cpp \
    src/messagesmodel.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
