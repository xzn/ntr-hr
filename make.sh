#!/usr/bin/env bash
make -j$(nproc) -C libctru/libctru lib/libctru.a
make rs
make -j$(nproc)
