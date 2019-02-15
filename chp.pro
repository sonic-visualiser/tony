TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}

!exists(config.pri) {
    include(noconfig.pri)
}

INCLUDEPATH += vamp-plugin-sdk

## !!! + vampGetPluginDescriptor export

CONFIG -= qt
CONFIG += plugin release warn_on

TARGET = chp

SOURCES += \
    chp/ConstrainedHarmonicPeak.cpp \
    chp/plugins.cpp \
    vamp-plugin-sdk/src/vamp-sdk/FFT.cpp \
    vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp \
    vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp

HEADERS += \
    chp/ConstrainedHarmonicPeak.h

