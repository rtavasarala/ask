QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = ask_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += SRC_DIR=\\\"$$PWD/../src\\\" TESTS_DIR=\\\"$$PWD\\\"

HEADERS += ../src/attachment.h ../src/attachmentsmodel.h ../src/commands.h ../src/context.h ../src/prompt.h ../src/settings.h ../src/backend.h ../src/instance.h ../src/messagesmodel.h ../src/theme.h ../src/translator.h
SOURCES += ask_tests.cpp ../src/attachment.cpp ../src/attachmentsmodel.cpp ../src/commands.cpp ../src/context.cpp ../src/prompt.cpp ../src/settings.cpp ../src/backend.cpp ../src/instance.cpp ../src/messagesmodel.cpp ../src/theme.cpp ../src/translator.cpp
