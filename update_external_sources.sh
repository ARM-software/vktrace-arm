#!/bin/bash

set -e

TARGET="x64"
BUILD_TYPE="debug"

print_help() {
    echo "Supported parameters are:"
    echo "    --target     <x86|x64|arm_32|arm_64>   (optional, default is x64)"
    echo "    --build-type <debug|release> (optional, default is debug)"
}

while [[ $# -gt 0 ]]
do
    case $1 in
        --target)
            TARGET=$2
            echo "Build for $TARGET"
            shift 2
            ;;
        --build-type)
            BUILD_TYPE=$2
            echo "Build type $BUILD_TYPE"
            shift 2
            ;;
        *)
            # unknown option
            echo Unknown option: $1
            print_help
            exit 1
            ;;
    esac
done

TARGET_LIST="x86 x64 arm_32 arm_64"
if ! [[ ${TARGET_LIST} =~ (^|[[:space:]])${TARGET}($|[[:space:]]) ]]; then
    echo "Unsupported target option ${TARGET}!"
    exit 1
fi

BUILD_TYPE_LIST="debug release"
if ! [[ ${BUILD_TYPE_LIST} =~ (^|[[:space:]])${BUILD_TYPE}($|[[:space:]]) ]]; then
    echo "Unsupported --build-type option ${BUILD_TYPE}!"
    exit 1
fi

BUILD_TYPE_OPTION=${BUILD_TYPE^}

if [[ $(uname) == "Linux" || $(uname) =~ "CYGWIN" || $(uname) =~ "MINGW" ]]; then
    CURRENT_DIR="$(dirname "$(readlink -f ${BASH_SOURCE[0]})")"
    CORE_COUNT=$(nproc || echo 4)
elif [[ $(uname) == "Darwin" ]]; then
    CURRENT_DIR="$(dirname "$(python -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' ${BASH_SOURCE[0]})")"
    CORE_COUNT=$(sysctl -n hw.ncpu || echo 4)
fi
echo CURRENT_DIR=$CURRENT_DIR
echo CORE_COUNT=$CORE_COUNT

BUILDDIR=${CURRENT_DIR}
BASEDIR="$BUILDDIR/submodules"

git submodule update --init --recursive

echo "Building ${BASEDIR}/jsoncpp"
cd ${BASEDIR}/jsoncpp
python amalgamate.py

mkdir -p $BUILDDIR/external

cd ${BUILDDIR}
python scripts/update_deps.py --config=${BUILD_TYPE_OPTION} --arch=${TARGET} --dir ${BUILDDIR}/external

