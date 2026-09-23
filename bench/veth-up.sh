#!/usr/bin/env sh
# veth test bench for xdp work. run with doas; idempotent.
#
# topology: netns xdpbench holds veth0 (10.99.0.2, client side).
# root ns holds veth1 (10.99.0.1, server side) - xdp attaches there.
# without the netns both ends are local addresses and the kernel short-
# circuits through `dev lo`; nothing ever crosses the veth pair.
set -e
IP="$(command -v ip || true)"
[ -n "$IP" ] || IP="$(ls -d /nix/store/*-iproute2-*/bin/ip 2>/dev/null | head -1)"

if ! "$IP" netns list | grep -q '^xdpbench'; then
    "$IP" netns add xdpbench
fi
if ! "$IP" link show veth1 >/dev/null 2>&1 && ! "$IP" -n xdpbench link show veth0 >/dev/null 2>&1; then
    "$IP" link add veth0 type veth peer name veth1
fi
# move the client end into the netns (re-pairs with veth1 across namespaces)
if ! "$IP" -n xdpbench link show veth0 >/dev/null 2>&1; then
    "$IP" link set veth0 netns xdpbench
fi

"$IP" -n xdpbench addr replace 10.99.0.2/24 dev veth0
"$IP" -n xdpbench link set veth0 up
"$IP" -n xdpbench link set lo up
"$IP" addr del 10.99.0.2/24 dev veth1 2>/dev/null || true
"$IP" addr del 10.99.0.2/24 dev veth1 2>/dev/null || true
"$IP" addr replace 10.99.0.1/24 dev veth1
"$IP" link set veth1 up
echo "veth bench up: root ns veth1 10.99.0.1 (server) <-> netns xdpbench veth0 10.99.0.2 (client)"
echo "--- netns:"; "$IP" -n xdpbench -br addr show veth0; "$IP" -n xdpbench route
echo "--- root:"; "$IP" -br addr show veth1
PROBE="$(nsenter --net=/var/run/netns/xdpbench bash -c 'cat < /dev/null > /dev/tcp/10.99.0.1/8080 && echo CONNECT_OK || echo CONNECT_FAIL' 2>&1)"
echo "probe: $PROBE"
