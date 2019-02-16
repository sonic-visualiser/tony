TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {
    include(noconfig.pri)
}

CONFIG -= qt
CONFIG += plugin release warn_on

TARGET = chp

INCLUDEPATH += vamp-plugin-sdk

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
    chp/ConstrainedHarmonicPeak.cpp \
    chp/plugins.cpp \
    vamp-plugin-sdk/src/vamp-sdk/FFT.cpp \
    vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp \
    vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp

HEADERS += \
    chp/ConstrainedHarmonicPeak.h

