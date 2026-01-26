#!/bin/bash

set -e

echo "[TEST] Basic GET"

OUT=$(curl -s -D - http://localhost:6969/index.html)

echo "$OUT" | grep "200 OK"
echo "$OUT" | grep "Hello"

