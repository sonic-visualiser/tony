FROM 32bit/ubuntu:14.04
MAINTAINER Chris Cannam <cannam@all-day-breakfast.com>
RUN apt-get update && apt-get install -y \
    build-essential \
    mercurial \
    qtbase5-dev qt5-default \
    libsndfile1-dev libsamplerate0-dev \
    libfftw3-dev \
    libbz2-dev \
    libjack-dev libjack0 libpulse-dev \
    libmad0-dev libid3tag0-dev \
    liboggz2-dev libfishsound1-dev \
    libasound2-dev \
    liblo-dev liblrdf0-dev libsord-dev libserd-dev \
    vamp-plugin-sdk librubberband-dev \
    libboost-dev libboost-test-dev \
    lintian \
    w3m
RUN hg clone -u v1.0-linuxbuild https://code.soundsoftware.ac.uk/hg/tony /opt/tony
WORKDIR /opt/tony
RUN ./configure && make
RUN make -C chp -f Makefile.linux
RUN make -C pyin -f Makefile.linux64
RUN bash deploy/linux/deploy-deb.sh 1.0cc1-1 i386
