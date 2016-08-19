
TEMPLATE = app

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {

    CONFIG += release
    DEFINES += NDEBUG BUILD_RELEASE NO_TIMING

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

    DEFINES += HAVE_BZ2 HAVE_FFTW3 HAVE_FFTW3F HAVE_SNDFILE HAVE_SAMPLERATE HAVE_VAMP HAVE_VAMPHOSTSDK HAVE_RUBBERBAND HAVE_LIBLO HAVE_MAD HAVE_ID3TAG

    LIBS += -lbz2 -lrubberband -lvamp-hostsdk -lfftw3 -lfftw3f -lsndfile -lFLAC -logg -lvorbis -lvorbisenc -lvorbisfile -logg -lmad -lid3tag -lsamplerate -llo -lz -lsord-0 -lserd-0

    win* {
        DEFINES += HAVE_PORTAUDIO_2_0
        LIBS += -lportaudio -lwinmm -lws2_32
    }
    macx* {
        DEFINES += HAVE_COREAUDIO HAVE_PORTAUDIO_2_0
        LIBS += -lportaudio -framework CoreAudio -framework CoreMidi -framework AudioUnit -framework AudioToolbox -framework CoreFoundation -framework CoreServices -framework Accelerate
    }
}

CONFIG += qt thread warn_on stl rtti exceptions c++11
QT += network xml gui widgets svg

TARGET = Tony
linux*:TARGET = tony
solaris*:TARGET = tony

DEPENDPATH += . bqaudioio svcore svgui svapp
INCLUDEPATH += . bqaudioio svcore svgui svapp

OBJECTS_DIR = o
MOC_DIR = o

ICON = tony.icns
RC_FILE = icons/tony.rc

contains(DEFINES, BUILD_STATIC):LIBS -= -ljack

MY_LIBS = -Lsvapp -Lsvgui -Lsvcore -Lchecker -Ldataquay -L. \
          -lsvapp -lsvgui -lsvcore -lchecker -ldataquay -lbq

linux* {
MY_LIBS = -Wl,-Bstatic $$MY_LIBS -Wl,-Bdynamic
}

win* {
MY_LIBS = -Lsvapp/release -Lsvgui/release -Lsvcore/release -Lchecker/release -Ldataquay/release -Lrelease $$MY_LIBS
}

LIBS = $$MY_LIBS $$LIBS

win32-msvc* {
PRE_TARGETDEPS += svapp/svapp.lib \
                  svgui/svgui.lib \
                  svcore/svcore.lib \
                  dataquay/dataquay.lib \
                  checker/checker.lib
}

win32-g++ {
PRE_TARGETDEPS += svapp/release/libsvapp.a \
                  svgui/release/libsvgui.a \
                  svcore/release/libsvcore.a \
                  dataquay/release/libdataquay.a \
                  checker/release/libchecker.a
}

!win* {
PRE_TARGETDEPS += svapp/libsvapp.a \
                  svgui/libsvgui.a \
                  svcore/libsvcore.a \
                  dataquay/libdataquay.a \
                  checker/libchecker.a
}

RESOURCES += tony.qrc

HEADERS += src/MainWindow.h \
           src/NetworkPermissionTester.h \
           src/Analyser.h

SOURCES += src/main.cpp \
           src/Analyser.cpp \
           src/NetworkPermissionTester.cpp \
           src/MainWindow.cpp

QMAKE_INFO_PLIST = deploy/osx/Info.plist



