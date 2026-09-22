# Android build environment for fallout2-ce
#
# Why this exists: the host machine has no Android SDK/NDK. Everything that
# produces an APK (gradle + cmake + NDK toolchain) runs inside this image so the
# host stays clean and the toolchain is reproducible.
#
# Platform note: Google only ships the Android NDK for linux-x86_64. On Apple
# Silicon the container must run as linux/amd64 (Docker Desktop's Rosetta/QEMU
# emulation). Building `--platform linux/amd64` is therefore mandatory, not
# optional.
#
# Build:
#   docker build --platform linux/amd64 -t fallout2-android-build \
#     -f docker/android-build/Dockerfile docker/android-build
#
# Usage (from the project root):
#   docker run --rm --platform linux/amd64 \
#     -v "$PWD":/project -w /project/os/android \
#     fallout2-android-build ./gradlew assembleDebug
#
# Output APK lands in os/android/app/build/outputs/apk/debug/ on the host.

FROM docker.m.daocloud.io/library/ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

# --- Environment -------------------------------------------------------------
# Two JDKs on purpose:
#   * JDK 11 -> Gradle/AGP 7.2.1 (the version this project's build.gradle pins)
#   * JDK 17 -> sdkmanager (shipped cmdline-tools are compiled for class v61)
# JAVA_HOME stays on 11 so `./gradlew` picks the right one by default.
ENV JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64
ENV JAVA_17_HOME=/usr/lib/jvm/java-17-openjdk-amd64
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_NDK_ROOT=/opt/android-sdk/ndk/23.2.8568313
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/23.2.8568313
ENV GRADLE_USER_HOME=/opt/gradle-home
ENV LANG=C.UTF-8

ENV PATH="${JAVA_HOME}/bin:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${ANDROID_SDK_ROOT}/build-tools/32.0.0:/opt/gradle/bin:${PATH}"

# --- Base packages -----------------------------------------------------------
# JDK 11 is what AGP 7.2.1 (used by this project) expects; JDK 17 is not
# guaranteed compatible with that AGP version.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        openjdk-11-jdk-headless \
        openjdk-17-jdk-headless \
        unzip zip wget curl git patch make ninja-build python3 \
        libc6-dev-i386 lib32z1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# --- Android command line tools ---------------------------------------------
ARG CMDLINE_TOOLS_VERSION=11076708
RUN mkdir -p /opt/android-sdk/cmdline-tools \
    && cd /tmp \
    && wget -q --tries=5 --timeout=60 \
        "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_VERSION}_latest.zip" -O cmdline-tools.zip \
    && unzip -q cmdline-tools.zip -d /opt/android-sdk/cmdline-tools \
    && mv /opt/android-sdk/cmdline-tools/cmdline-tools /opt/android-sdk/cmdline-tools/latest \
    && rm -f cmdline-tools.zip

# --- SDK components ----------------------------------------------------------
# Versions pinned to match os/android/app/build.gradle:
#   compileSdk 32, targetSdk 32, AGP 7.2.1
# NDK 23.2.8568313 is the last NDK fully validated with AGP 7.2.x.
# sdkmanager needs JDK 17; flip JAVA_HOME for the install block, then flip back
# to 11 so `./gradlew` picks the AGP-compatible JDK at runtime.
ENV JAVA_HOME=${JAVA_17_HOME}

RUN (yes | sdkmanager --licenses > /dev/null 2>&1 || true) \
    && sdkmanager --install \
        "platform-tools" \
        "platforms;android-32" \
        "build-tools;32.0.0" \
        "ndk;23.2.8568313" \
        "cmake;3.22.1" \
    && sdkmanager --list_installed

ENV JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64

# --- Workspace ---------------------------------------------------------------
# GRADLE_USER_HOME is baked into the image so dependency caches survive across
# `docker run` invocations (mount a volume here if you want them on the host).
RUN mkdir -p /opt/gradle-home /project

WORKDIR /project

# Default: print the toolchain. `sdkmanager` itself needs JDK 17.
CMD ["bash", "-c", "java -version 2>&1 | head -1; echo \"SDK: $ANDROID_SDK_ROOT\"; echo \"NDK: $ANDROID_NDK_HOME\"; JAVA_HOME=$JAVA_17_HOME sdkmanager --list_installed"]
