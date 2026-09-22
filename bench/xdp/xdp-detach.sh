#!/usr/bin/env sh
# detach from veth1 and drop the pins. run with doas.
set -e
BPFTOOL="$(command -v bpftool || true)"
[ -n "$BPFTOOL" ] || BPFTOOL="$(ls -d /nix/store/*-bpftools-*/bin/bpftool 2>/dev/null | head -1)"
"$BPFTOOL" net detach xdp dev veth1 2>/dev/null || true
rm -rf /sys/fs/bpf/httpbench
rm -f /sys/fs/bpf/pkts
echo "xdp detached"
