#!/bin/bash
set -e

# Clean and build loader
# Always use GCC for loader (TCC has ABI issues with lws callbacks)
# app.so is rebuilt by loader using TCC (no lws calls, so TCC is fine)
rm -f app.so loader dashboard
gcc -O0 -g -o loader src/loader.c -ldl -lpthread -lwebsockets
echo "Built loader (gcc)"
