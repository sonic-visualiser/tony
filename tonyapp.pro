
TEMPLATE = app

win32-g++ {
    INCLUDEPATH += sv-dependency-builds/win32-mingw/include
    LIBS += -Lsv-dependency-builds/win32-mingw/lib
}
win32-msvc* {
    INCLUDEPATH += sv-dependency-builds/win32-msvc/include
    LIBS += -Lsv-dependency-builds/win32-msvc/lib
}

exists(config.pri) {
    include(config.pri)
}
win* {
    !exists(config.pri) {
        DEFINES += HAVE_BZ2 HAVE_FFTW3 HAVE_FFTW3F HAVE_SNDFILE HAVE_SAMPLERATE HAVE_VAMP HAVE_VAMPHOSTSDK HAVE_RUBBERBAND HAVE_DATAQUAY HAVE_LIBLO HAVE_MAD HAVE_ID3TAG HAVE_PORTAUDIO_2_0
        LIBS += -lbz2 -lrubberband -lvamp-hostsdk -lfftw3 -lfftw3f -lsndfile -lFLAC -logg -lvorbis -lvorbisenc -lvorbisfile -logg -lmad -lid3tag -lportaudio -lsamplerate -llo -lz -lsord-0 -lserd-0 -lwinmm -lws2_32
    }
}

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

win* {
MY_LIBS = -Lsvapp/release -Lsvgui/release -Lsvcore/release -Ldataquay/release $$MY_LIBS
}

LIBS = $$MY_LIBS $$LIBS

win32-msvc* {
PRE_TARGETDEPS += svapp/svapp.lib \
                  svgui/svgui.lib \
                  svcore/svcore.lib \
                  dataquay/dataquay.lib
}

win32-g++ {
PRE_TARGETDEPS += svapp/release/libsvapp.a \
                  svgui/release/libsvgui.a \
                  svcore/release/libsvcore.a \
                  dataquay/release/libdataquay.a
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



