#!/bin/bash

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
mkdir -p build
cd build

BUILD_TYPE="None"
BUILD=false
RUN=false

while getopts ":rbt:" opt; do
    case $opt in
    r)
        RUN=true
        ;;
    b)
        BUILD=true
        ;;
    t)
        BUILD=true
        BUILD_TYPE="$OPTARG"
        ;;
    \?)
        echo "Invalid option: -$OPTARG" >&2
        exit 1
        ;;
    :)
        echo "Option -$OPTARG requires an argument." >&2
        exit 1
        ;;
    esac
done

shift $((OPTIND - 1))

if [[ $BUILD_TYPE != "None" ]]; then
    cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..
fi

if $BUILD; then
    cmake --build . -j 12
fi

if $RUN; then
    ./leveldb_tests
fi
