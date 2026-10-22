# Build environment for the Mru F´ deployment: native Linux builds and unit
# tests, and the static 32-bit ARM build for OPS-SAT PRETTY's SEPP
# (Cortex-A9, hard float). Used by scripts/fprime-docker.sh; the build
# context is the fprime folder: docker build -f fprime/toolchain/Dockerfile fprime
FROM rust:1-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 python3-venv python3-pip g++ g++-arm-linux-gnueabihf \
        git && rm -rf /var/lib/apt/lists/*
RUN rustup target add armv7-unknown-linux-gnueabihf
# the repository is mounted from the host, owned by another user; F´ reads
# its version with git describe
RUN git config --system --add safe.directory '*'
COPY requirements.txt /tmp/req/requirements.txt
COPY lib/fprime/requirements.txt /tmp/req/lib/fprime/requirements.txt
RUN python3 -m venv /opt/fprime-venv && /opt/fprime-venv/bin/pip install --no-cache-dir -r /tmp/req/requirements.txt
ENV PATH=/opt/fprime-venv/bin:$PATH
