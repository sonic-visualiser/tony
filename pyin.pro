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

#!!! boost path fix plz
INCLUDEPATH += vamp-plugin-sdk ../boost_1_69_0

win32-msvc* {
    LIBS += -EXPORT:vampGetPluginDescriptor
}
win32-g++* {
    LIBS += -Wl,--version-script=pyin/vamp-plugin.map
}
linux* {
    LIBS += -Wl,--version-script=pyin/vamp-plugin.map
}
macx* {
    LIBS += -export_symbols_list pyin/vamp-plugin.list
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

