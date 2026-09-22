#!/usr/bin/env sh
# load pass.bpf.o and attach it to veth1. run with doas.
set -e
cd "$(dirname "$0")"
BPFTOOL="$(command -v bpftool || true)"
[ -n "$BPFTOOL" ] || BPFTOOL="$(ls -d /nix/store/*-bpftools-*/bin/bpftool 2>/dev/null | head -1)"
if ! mountpoint -q /sys/fs/bpf 2>/dev/null; then
    mount -t bpf bpf /sys/fs/bpf
fi
mkdir -p /sys/fs/bpf/httpbench
rm -f /sys/fs/bpf/pkts
"$BPFTOOL" prog loadall pass.bpf.o /sys/fs/bpf/httpbench
"$BPFTOOL" net attach xdp pinned /sys/fs/bpf/httpbench/pass dev veth1
echo "xdp pass attached to veth1; counter resets per attach"
