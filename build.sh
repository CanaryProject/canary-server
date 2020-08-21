#!/usr/bin/env bash
TYPE="Debug"
TESTS="OFF"
EXECUTE="OFF"
WARNING_FLAGS="OFF"
OPTIMIZATIONS="ON"
BENCHMARKING="OFF"

usage()
{
    ./canary_echo.sh "Usage: -e for execute, -r for release, -s for silent, -t for build tests and -v for verbose]]"
    exit 1
}

while getopts "behrstv" opt; do
  case "$opt" in
    b) BENCHMARKING="ON" ;;
    e) EXECUTE="ON" ;;
    h) usage ;;
    r) TYPE="Release" ;;
    s) WARNING_FLAGS="OFF" OPTIMIZATIONS="OFF" ;;
    t) TESTS="ON" ;;
    v) WARNING_FLAGS="ON" OPTIMIZATIONS="ON" ;;
  esac
done

cd build
cmake -DOPTIONS_ENABLE_UNIT_TEST=${TESTS} -DCMAKE_BUILD_TYPE=${TYPE} -DOPTIONS_ENABLE_IPO=${OPTIMIZATIONS} -DOPTIONS_WARNINGS_FLAGS=${WARNING_FLAGS} -DOPTIONS_ENABLE_BENCHMARKING=${BENCHMARKING} .. || exit 1; make -j`nproc` || exit 1
cd ..
rm -rf canary
cp build/bin/canary ./

if [ "$EXECUTE" = "ON" ]; then
  ./canary
fi
