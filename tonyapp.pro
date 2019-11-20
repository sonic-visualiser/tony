
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

linux* {

    tony_bins.path = $$PREFIX_PATH/bin/
    tony_bins.files = tony
    tony_bins.CONFIG = no_check_exist executable

    tony_support.path = $$PREFIX_PATH/lib/tony/
    tony_support.files = chp.so pyin.so
    tony_support.CONFIG = no_check_exist executable

    tony_desktop.path = $$PREFIX_PATH/share/applications/
    tony_desktop.files = tony.desktop
    tony_desktop.CONFIG = no_check_exist

    tony_icon.path = $$PREFIX_PATH/share/icons/hicolor/scalable/apps/
    tony_icon.files = icons/tony-icon.svg
    tony_icon.CONFIG = no_check_exist
    
    INSTALLS += tony_bins tony_support tony_desktop tony_icon
}

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

macx* {
    QMAKE_POST_LINK += deploy/osx/deploy.sh Tony
}
