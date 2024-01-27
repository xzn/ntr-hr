#!/usr/bin/env bash
make -j$(nproc) -C libctru/libctru lib/libctru.a
make -j$(nproc)
