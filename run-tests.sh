#!/usr/bin/env bash

TESTS="ALL"

ORANGE='\033[0;33m'
NC='\033[0m' # No Color
CANARY_PROJECT="${ORANGE}[CanaryProject]${NC}"

usage()
{
    echo -e "${CANARY_PROJECT} Usage: [--all, --server, --lib]"
    echo -e "${CANARY_PROJECT} You can pass tests arguments, like -s, --help or -d yes"
    exit 1
}

while getopts ":-:h" opt; do
  case ${opt} in
    -) 
      case ${OPTARG} in
        "all"*) TESTS="ALL"
            ;;
        "lib"*) TESTS="LIB"
            ;;
        "server"*) TESTS="SERVER"
            ;;
      esac 
    ;;
    h) usage ;;
  esac
done

# useful arguments --success -d yes

if [ ${TESTS} != "LIB" ]; then
  echo -e "${CANARY_PROJECT} Running server tests"
  ./build/bin/canary-tests $@
fi

if [ ${TESTS} != "SERVER" ]; then
  echo -e "${CANARY_PROJECT} Running lib tests"
  ./canary-lib/tests/canary-lib-tests $@
fi