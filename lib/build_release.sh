#!/usr/bin/env bash

DATE=$(date '+%y%m%d%H%M%S')
SRC=$(pwd)

INSTALL_DIR="$SRC/${DATE}_release.install"
BUILD_DIR="$SRC/${DATE}_release.build"

mkdir -p "$INSTALL_DIR"

echo "install: $INSTALL_DIR, build: $BUILD_DIR"

cmake --install-prefix / -D CMAKE_BUILD_TYPE=Release -S . -B "$BUILD_DIR"

ln -fs -T "$INSTALL_DIR" install
ln -fs -T "$BUILD_DIR" build

cd "$BUILD_DIR" || exit

cmake --build .
cmake --install . --prefix "$INSTALL_DIR"

cd "$SRC" || exit

mkdir -p xchg
cp -r -t xchg install/*
