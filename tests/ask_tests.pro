QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = ask_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += SRC_DIR=\\\"$$PWD/../src\\\" TESTS_DIR=\\\"$$PWD\\\"

HEADERS += ../src/backend.h ../src/instance.h ../src/messagesmodel.h ../src/theme.h ../src/translator.h
SOURCES += ask_tests.cpp ../src/backend.cpp ../src/instance.cpp ../src/messagesmodel.cpp ../src/theme.cpp ../src/translator.cpp
