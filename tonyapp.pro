
TEMPLATE = app

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {
    include(noconfig.pri)
}

include(base.pri)

QT += network xml gui widgets svg

TARGET = Tony
linux*:TARGET = tony
solaris*:TARGET = tony

OBJECTS_DIR = o
MOC_DIR = o

ICON = tony.icns
RC_FILE = icons/tony.rc

RESOURCES += tony.qrc

QMAKE_INFO_PLIST = deploy/osx/Info.plist

include(svgui/files.pri)
include(svapp/files.pri)

for (file, SVGUI_SOURCES)    { SOURCES += $$sprintf("svgui/%1",    $$file) }
for (file, SVAPP_SOURCES)    { SOURCES += $$sprintf("svapp/%1",    $$file) }

for (file, SVGUI_HEADERS)    { HEADERS += $$sprintf("svgui/%1",    $$file) }
for (file, SVAPP_HEADERS)    { HEADERS += $$sprintf("svapp/%1",    $$file) }

HEADERS += main/MainWindow.h \
           main/NetworkPermissionTester.h \
           main/Analyser.h

SOURCES += main/main.cpp \
           main/Analyser.cpp \
           main/NetworkPermissionTester.cpp \
           main/MainWindow.cpp



