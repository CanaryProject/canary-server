#!/usr/bin/env bash
TYPE="Debug"
TESTS="Off"
EXECUTE="On"

usage()
{
    echo "usage: ./build.sh [[-r Debug/Release] [-t On/Off] [-e Off]]"
}

while getopts ":r:t:e:" opt; do
  case "$opt" in
    r) 
      TYPE=$OPTARG 
      if [ "$TYPE" != "Debug" -a "$TYPE" != "Release" ]; then
        usage
        exit 1
      fi
      ;;
    t) TESTS=$OPTARG 
      if [ "$TESTS" != "On" -a "$TESTS" != "Off" ]; then
        usage
        exit 1
      fi
    ;;
    e) EXECUTE=$OPTARG;;
  esac
done

cd build
sudo cmake -DOPTIONS_ENABLE_UNIT_TEST=${TESTS} -DCMAKE_BUILD_TYPE=${TYPE} .. ; make
cd ..
rm -rf canary
cp build/bin/canary ./

if [ "$EXECUTE" != "Off" ]; then
  ./canary
fi