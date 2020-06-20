#!/usr/bin/env bash
# $ vnc-build -a [16..25] -w

set -e

DAEMON_BUILD_PATH=libs/armeabi-v7a
LIB_BUILD_PATH=nativeMethods/libs/armeabi-v7a
AOSP_PATH=~/android/aosp
CWD=$(pwd)
JOBS=$(grep 'processor' /proc/cpuinfo | wc -l)

usage() {
    echo "Usage: $0 -c -w -a API_LEVEL"
}

clean() {
    rm ${DAEMON_BUILD_PATH}/* &> /dev/null || true
}

build_wrapper() {
    cd $AOSP_PATH

    source build/envsetup.sh
    lunch aosp_arm-eng

    SDK=$(grep "^\s*PLATFORM_SDK_VERSION" build/core/version_defaults.mk | awk '{print $3}')

    rm -rf external/nativeMethods &> /dev/null
    mkdir -p external/nativeMethods
    cp -r ${CWD}/nativeMethods/* external/nativeMethods
    cd external/nativeMethods

    echo
    echo Erstelle libdvnc_flinger_sdk${SDK}.so
    [ -n "$1" ] && mma clean
    mma -j$JOBS
    cd $CWD
    cp -f $AOSP_PATH/out/target/product/generic/system/lib/libdvnc_flinger_sdk${SDK}.so $LIB_BUILD_PATH/libdvnc_flinger_sdk${SDK}.so
}

while getopts wc o; do
    case $o in
        w)  WRAPPER_LIB='yes' ;;
        c)  CLEAN_ALL='yes' ;;
        \?) usage
            exit 1 ;;
    esac
done

clean

if [ -n "$WRAPPER_LIB" ]; then
    build_wrapper $CLEAN_ALL
    exit 1
fi

if [ -n "$CLEAN_ALL" ]; then 
    ndk-build clean
    exit 1
fi

ndk-build -j $JOBS
cp ${DAEMON_BUILD_PATH}/androidvncserver ~/Schreibtisch/androidvncserver2

