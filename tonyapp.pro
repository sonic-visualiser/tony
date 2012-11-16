
TEMPLATE = app

include(config.pri)

CONFIG += qt thread warn_on stl rtti exceptions
QT += network xml gui

TARGET = Tony
linux*:TARGET = tony
solaris*:TARGET = tony

DEPENDPATH += . svcore svgui svapp
INCLUDEPATH += . svcore svgui svapp

OBJECTS_DIR = o
MOC_DIR = o

contains(DEFINES, BUILD_STATIC):LIBS -= -ljack

LIBS = -Lsvapp -Lsvgui -Lsvcore -lsvapp -lsvgui -lsvcore $$LIBS

win* {
PRE_TARGETDEPS += svapp/svapp.lib \
                  svgui/svgui.lib \
                  svcore/svcore.lib
}
!win* {
PRE_TARGETDEPS += svapp/libsvapp.a \
                  svgui/libsvgui.a \
                  svcore/libsvcore.a
}

RESOURCES += tony.qrc

HEADERS += src/MainWindow.h \
           src/Analyser.h

SOURCES += src/main.cpp \
           src/Analyser.cpp \
           src/MainWindow.cpp

QMAKE_INFO_PLIST = deploy/osx/Info.plist



