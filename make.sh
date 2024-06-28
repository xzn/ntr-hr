#!/usr/bin/env bash
make -j$(nproc) -C libctru/libctru lib/libctru.a
make target/armv6k-nintendo-3ds/release/libnwm_rs.a
make -j$(nproc) "$@"
