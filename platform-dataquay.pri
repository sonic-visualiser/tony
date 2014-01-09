
exists(config.pri) {
    include(./config.pri)
}

!exists(config.pri) {
    win32-g++ {
        INCLUDEPATH += sv-dependency-builds/win32-mingw/include
        LIBS += -Lsv-dependency-builds/win32-mingw/lib
    }
    win32-msvc* {
        INCLUDEPATH += sv-dependency-builds/win32-msvc/include
        LIBS += -Lsv-dependency-builds/win32-msvc/lib
    }
    macx* {
        INCLUDEPATH += sv-dependency-builds/osx/include
        LIBS += -Lsv-dependency-builds/osx/lib
    }
}

CONFIG += staticlib

DEFINES -= USE_REDLAND
QMAKE_CXXFLAGS -= -I/usr/include/rasqal -I/usr/include/raptor2
EXTRALIBS -= -lrdf

DEFINES += USE_SORD
# Libraries and paths should be added by config.pri

