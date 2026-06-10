TEMPLATE = app
TARGET = openhbbtvbrowser
QT += webenginewidgets
CONFIG += c++1z
CONFIG += console

DEFINES += EMBEDDED_BUILD

HEADERS += \
    virtualkey.h \
    browsercontrol.h \
    browserwindow.h \
    webpage.h \
    webview.h \
    hardwareprofile.h

SOURCES += \
    main.cpp \
    browsercontrol.cpp \
    browserwindow.cpp \
    webpage.cpp \
    webview.cpp \
    hardwareprofile.cpp

RESOURCES += openhbbtvbrowser.qrc
