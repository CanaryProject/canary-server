#!/usr/bin/env bash
TYPE="Debug"
TESTS="Off"
EXECUTE="Off"
WARNING_FLAGS="Off"
REMOVE_EXTERNAL_WARNINGS="On"

usage()
{
    ./canary_echo.sh "Usage: -e for execute, -r for release, -t for build tests and -v for verbose]]"
    exit 1
}

while getopts "ehrtv" opt; do
  case "$opt" in
    e) EXECUTE="On" ;;
    h) usage ;;
    r) TYPE="Release" ;;
    t) TESTS="On" ;;
    v) WARNING_FLAGS="On" REMOVE_EXTERNAL_WARNINGS="Off" ;;
  esac
done

cd build
sudo cmake -DOPTIONS_ENABLE_UNIT_TEST=${TESTS} -DOPTIONS_ENABLE_IPO={REMOVE_EXTERNAL_WARNINGS} -DOPTIONS_WARNINGS_FLAGS={WARNING_FLAGS} -DCMAKE_BUILD_TYPE=${TYPE} .. ; make
cd ..
rm -rf canary
cp build/bin/canary ./

if [ "$EXECUTE" = "On" ]; then
  ./canary
fi