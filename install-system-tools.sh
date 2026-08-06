#!/usr/bin/env bash
set -euo pipefail
# System packages for GOF2HD reverse-engineering / cross-build (Manjaro/Arch)
pacman -Syu --needed --noconfirm \
  base-devel \
  git \
  jre-openjdk-headless \
  rizin \
  qemu-user-static \
  qemu-user-static-binfmt \
  android-tools \
  llvm \
  cmake \
  ninja \
  python \
  python-pip \
  python-virtualenv \
  unrar \
  p7zip \
  wget \
  openssh \
  rsync

# enable binfmt for qemu-arm (lets host execute armhf binaries transparently)
systemctl enable --now qemu-binfmt.service || true

echo "DONE: system tools installed"
