FROM ubuntu:14.04
MAINTAINER Chris Cannam <cannam@all-day-breakfast.com>
RUN apt-get update
RUN apt-get install -y build-essential
RUN apt-get install -y mercurial
RUN apt-get install -y qtbase5-dev qt5-default
RUN apt-get install -y libsndfile1-dev libsamplerate0-dev libfftw3-dev libbz2-dev libjack-dev libjack0 libpulse-dev libmad0-dev libid3tag0-dev liboggz2-dev libfishsound1-dev libasound2-dev liblo-dev liblrdf0-dev libsord-dev libserd-dev
RUN apt-get install -y vamp-plugin-sdk librubberband-dev
RUN apt-get install -y lintian
RUN hg clone -u v1.0-linuxbuild https://code.soundsoftware.ac.uk/hg/tony
