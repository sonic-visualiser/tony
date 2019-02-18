TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {
    include(noconfig.pri)
}

CONFIG -= qt
CONFIG += plugin release warn_on

TARGET = pyin

INCLUDEPATH += $$PWD/vamp-plugin-sdk

win32-msvc* {
    LIBS += -EXPORT:vampGetPluginDescriptor
}
win32-g++* {
    LIBS += -Wl,--version-script=$$PWD/pyin/vamp-plugin.map
}
linux* {
    LIBS += -Wl,--version-script=$$PWD/pyin/vamp-plugin.map
}
macx* {
    LIBS += -exported_symbols_list $$PWD/pyin/vamp-plugin.list
}

SOURCES += \
    pyin/YinUtil.cpp \
    pyin/Yin.cpp \
    pyin/SparseHMM.cpp \
    pyin/MonoPitchHMM.cpp \
    pyin/MonoNoteParameters.cpp \
    pyin/MonoNoteHMM.cpp \
    pyin/MonoNote.cpp \
    pyin/libmain.cpp \
    pyin/YinVamp.cpp \
    pyin/PYinVamp.cpp \
    pyin/LocalCandidatePYIN.cpp \
    vamp-plugin-sdk/src/vamp-sdk/FFT.cpp \
    vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp \
    vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp

HEADERS += \
    pyin/YinUtil.h \
    pyin/Yin.h \
    pyin/SparseHMM.h \
    pyin/MonoPitchHMM.h \
    pyin/MonoNoteParameters.h \
    pyin/MonoNoteHMM.h \
    pyin/MonoNote.h \
    pyin/MeanFilter.h \
    pyin/YinVamp.h \
    pyin/PYinVamp.h \
    pyin/LocalCandidatePYIN.h

