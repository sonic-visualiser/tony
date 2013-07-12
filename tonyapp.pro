
TEMPLATE = app

include(config.pri)

CONFIG += qt thread warn_on stl rtti exceptions
QT += network xml gui widgets

TARGET = Tony
linux*:TARGET = tony
solaris*:TARGET = tony

DEPENDPATH += . svcore svgui svapp
INCLUDEPATH += . svcore svgui svapp

OBJECTS_DIR = o
MOC_DIR = o

contains(DEFINES, BUILD_STATIC):LIBS -= -ljack

MY_LIBS = -Lsvapp -Lsvgui -Lsvcore -Ldataquay -lsvapp -lsvgui -lsvcore -ldataquay

linux* {
MY_LIBS = -Wl,-Bstatic $$MY_LIBS -Wl,-Bdynamic
}

LIBS = $$MY_LIBS $$LIBS

win* {
PRE_TARGETDEPS += svapp/svapp.lib \
                  svgui/svgui.lib \
                  svcore/svcore.lib \
                  dataquay/dataquay.lib
}
!win* {
PRE_TARGETDEPS += svapp/libsvapp.a \
                  svgui/libsvgui.a \
                  svcore/libsvcore.a \
                  dataquay/libdataquay.a
}

RESOURCES += tony.qrc

HEADERS += src/MainWindow.h \
           src/Analyser.h

SOURCES += src/main.cpp \
           src/Analyser.cpp \
           src/MainWindow.cpp

QMAKE_INFO_PLIST = deploy/osx/Info.plist



