FROM debian:12.11-slim@sha256:b1a741487078b369e78119849663d7f1a5341ef2768798f7b7406c4240f86aef

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      binutils build-essential ca-certificates cmake git libdrm-dev \
      libsystemd-dev libudev-dev libx11-dev libxrandr-dev patchelf \
      dbus-daemon pkg-config qt6-base-dev qt6-base-dev-tools rsync zstd \
 && rm -rf /var/lib/apt/lists/*
