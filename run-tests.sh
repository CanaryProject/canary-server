#!/usr/bin/env bash

echo "Running server tests"
./build/bin/canary-tests --reporter compact --success -d yes

echo "Running lib tests"
./canary-lib/tests/canary-lib-tests --reporter compact --success -d yes