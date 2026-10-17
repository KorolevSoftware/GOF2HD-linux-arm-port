# Build with --platform linux/arm/v7 (the Android engine is 32-bit ARM).
# This is the Debian base used for the tested ROCKNIX build.
FROM debian:bookworm-slim@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587

RUN test "$(dpkg --print-architecture)" = armhf \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        gcc g++ cmake make libsdl2-dev python3 unzip \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc \
    CXX=g++ \
    GOF_SYS_LIB=/usr/lib/arm-linux-gnueabihf

WORKDIR /work
CMD ["bash", "/work/port/tools/build-native.sh"]
