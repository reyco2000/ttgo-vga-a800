#!/bin/bash
# Host-side unit tests: compile each test with g++ into a scratch dir and run it.
set -e
cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

run() {  # run <name> <sources...>
  local name=$1; shift
  g++ -std=c++17 -Wall -Werror "$@" -o "$OUT/$name"
  "$OUT/$name"
}

run browser_test src/ui/browser.cpp tests/browser_test.cpp
echo "ALL HOST TESTS PASSED"
