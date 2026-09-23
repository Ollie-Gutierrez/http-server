#!/usr/bin/env sh
# veth test bench for xdp work. run with doas; idempotent.
# veth0 = client side, veth1 = server side (xdp attaches here)
set -e
IP="$(command -v ip || true)"
[ -n "$IP" ] || IP="$(ls -d /nix/store/*-iproute2-*/bin/ip 2>/dev/null | head -1)"
if ! "$IP" link show veth0 >/dev/null 2>&1; then
    "$IP" link add veth0 type veth peer name veth1
fi
"$IP" addr replace 10.99.0.1/24 dev veth0
"$IP" addr replace 10.99.0.2/24 dev veth1
"$IP" link set veth0 up
"$IP" link set veth1 up
echo "veth bench up: 10.99.0.1 (client) <-> 10.99.0.2 (server)"
