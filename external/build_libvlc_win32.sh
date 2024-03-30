#!/bin/bash

if [ "$BUILDING_LIBVLC_INSTALL_DEPS" == "1" ]
sudo apt-get update -qq
sudo apt-get install -qqy \
    git wget bzip2 file libwine-dev unzip libtool libtool-bin libltdl-dev pkg-config ant \
    build-essential automake texinfo ragel yasm p7zip-full autopoint \
    gettext cmake zip wine nsis g++-mingw-w64-i686 curl gperf flex bison \
    libcurl4-gnutls-dev python3 python3-setuptools python3-mako python3-requests \
    gcc make procps ca-certificates \
    openjdk-11-jdk-headless nasm jq gnupg \
    meson autoconf
sudo apt-get install -qqy \
    gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 mingw-w64-tools

fi

