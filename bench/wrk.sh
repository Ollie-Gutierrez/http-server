#!/usr/bin/env bash
set -euo pipefail

host="${1:-127.0.0.1}"
port="${2:-8080}"
threads="${WRK_THREADS:-2}"
connections="${WRK_CONNECTIONS:-100}"
duration="${WRK_DURATION:-10s}"

url="http://${host}:${port}/hello"
echo "# $url  (threads=$threads connections=$connections duration=$duration)"
wrk -t"$threads" -c"$connections" -d"$duration" "$url"
