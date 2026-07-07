#!/usr/bin/env bash
# Profile a running http_server with perf while applying wrk load.
# Usage: ./bench/perf.sh <server-pid> [port]
#
# For useful symbols, build with debug info first:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
set -euo pipefail

pid="${1:?usage: perf.sh <server-pid> [port]}"
port="${2:-8080}"
data="${PERF_DATA:-perf.data}"
seconds="${PERF_SECONDS:-5}"

perf record -F 999 --call-graph dwarf -p "$pid" -o "$data" -- sleep "$seconds" &
record=$!

./bench/wrk.sh 127.0.0.1 "$port" >/dev/null

wait "$record"
echo "# $data"
perf report -i "$data" --stdio --no-children | head -40
