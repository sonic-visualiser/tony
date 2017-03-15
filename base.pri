
SV_INCLUDEPATH = \
        . \
	bqvec \
	bqvec/bqvec \
	bqfft \
	bqresample \
	bqaudioio \
	bqaudioio/bqaudioio \
	piper-cpp \
	dataquay \
	dataquay/dataquay \
	svcore \
	svcore/data \
	svcore/plugin/api/alsa \
	svgui \
	svapp \
	vamp-plugin-sdk

DEPENDPATH += $$SV_INCLUDEPATH
INCLUDEPATH += $$SV_INCLUDEPATH

# Platform defines for RtMidi
linux*:   DEFINES += __LINUX_ALSASEQ__ __LINUX_ALSA__
macx*:    DEFINES += __MACOSX_CORE__
win*:     DEFINES += __WINDOWS_MM__
solaris*: DEFINES += __RTMIDI_DUMMY_ONLY__

# Defines for Dataquay
DEFINES += USE_SORD

CONFIG += qt thread warn_on stl rtti exceptions c++11

include(bq-files.pri)
include(vamp-plugin-sdk-files.pri)
include(svcore/files.pri)

DATAQUAY_SOURCES=$$fromfile(dataquay/lib.pro, SOURCES)
DATAQUAY_HEADERS=$$fromfile(dataquay/lib.pro, HEADERS)

for (file, BQ_SOURCES)       { SOURCES += $$file }
for (file, BQ_HEADERS)       { HEADERS += $$file }

for (file, VAMP_SOURCES)     { SOURCES += $$file }
for (file, VAMP_HEADERS)     { HEADERS += $$file }

for (file, DATAQUAY_SOURCES) { SOURCES += $$sprintf("dataquay/%1", $$file) }
for (file, DATAQUAY_HEADERS) { HEADERS += $$sprintf("dataquay/%1", $$file) }

for (file, SVCORE_SOURCES)   { SOURCES += $$sprintf("svcore/%1", $$file) }
for (file, SVCORE_HEADERS)   { HEADERS += $$sprintf("svcore/%1", $$file) }
    
