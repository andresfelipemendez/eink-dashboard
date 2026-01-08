FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    gcc \
    tcc \
    clang \
    clangd \
    libwebsockets-dev \
    libssl-dev \
    libpq-dev \
    libargon2-dev \
    postgresql-client \
    inotify-tools \
    gdb \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
