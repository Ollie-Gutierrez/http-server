#!/usr/bin/env sh
# run wrk as the normal user inside the xdpbench netns. run with doas.
# usage: xdp-wrk.sh [rounds] [url]
set -e
WRK="$(ls -d /nix/store/*-wrk-4*/bin/wrk 2>/dev/null | head -1)"
BASH="$(ls -d /nix/store/*-bash-5*/bin/bash 2>/dev/null | head -1)"
NSENTER="$(command -v nsenter || echo /run/current-system/sw/bin/nsenter)"
SETPRIV="$(command -v setpriv || echo /run/current-system/sw/bin/setpriv)"
ROUNDS="${1:-3}"
URL="${2:-http://10.99.0.1:8080/hello}"

"$NSENTER" --net=/var/run/netns/xdpbench "$SETPRIV" --reuid=1000 --regid=100 --clear-groups \
    "$BASH" -c "for i in \$(seq $ROUNDS); do $WRK -t2 -c50 -d10s $URL \
        | awk '/Requests.sec/ {printf \"round %s reqs\\n\", \$2}'; done"
