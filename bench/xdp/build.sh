#!/usr/bin/env sh
# compile the bpf objects. the nix clang wrapper injects x86 hardening flags
# the bpf target rejects, so use the unwrapped compiler from the store.
set -e
cd "$(dirname "$0")"
CC="$(ls -d /nix/store/*-clang-*/bin/clang 2>/dev/null | head -1)"
[ -n "$CC" ] || CC=clang
LIBBPF_INC="$(pkg-config --variable=includedir libbpf)"
GLIBC_INC="$(ls -d /nix/store/*-glibc-*-dev/include 2>/dev/null | head -1)"
$CC --target=bpf -O2 -g -Wall -I"$LIBBPF_INC" -I"$GLIBC_INC" -c pass.bpf.c -o pass.bpf.o
[ -s pass.bpf.o ] && echo "pass.bpf.o ok"
