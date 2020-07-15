#!/usr/bin/env bash
TYPE="Debug"
TESTS="Off"
EXECUTE="Off"

ORANGE='\033[0;33m'
NC='\033[0m' # No Color
CANARY_PROJECT="${ORANGE}[CanaryProject]${NC}"

usage()
{
    echo -e "${CANARY_PROJECT} Usage: -r for release, -t for build tests and -e for execute]]"
    exit 1
}

while getopts "rteh" opt; do
  case "$opt" in
    r) TYPE="Release" ;;
    t) TESTS="On" ;;
    e) EXECUTE="On" ;;
    h) usage ;;
  esac
done

cd build
sudo cmake -DOPTIONS_ENABLE_UNIT_TEST=${TESTS} -DCMAKE_BUILD_TYPE=${TYPE} .. ; make
cd ..
rm -rf canary
cp build/bin/canary ./

if [ "$EXECUTE" = "On" ]; then
  ./canary
fi