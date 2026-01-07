FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    tcc \
    clang \
    libwebsockets-dev \
    libssl-dev \
    inotify-tools \
    gdb \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
