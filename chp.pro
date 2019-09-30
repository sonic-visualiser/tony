TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {
    include(noconfig.pri)
}

CONFIG -= qt
CONFIG += plugin no_plugin_name_prefix release warn_on

TARGET = chp

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
    chp/ConstrainedHarmonicPeak.cpp \
    chp/plugins.cpp \
    vamp-plugin-sdk/src/vamp-sdk/FFT.cpp \
    vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp \
    vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp

HEADERS += \
    chp/ConstrainedHarmonicPeak.h

