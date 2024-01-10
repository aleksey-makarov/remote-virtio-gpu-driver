#!/usr/bin/env bash

DATE=$(date '+%y%m%d%H%M%S')
SRC=$(pwd)

INSTALL_DIR="$SRC/$DATE.install"
BUILD_DIR="$SRC/$DATE.build"

mkdir -p "$INSTALL_DIR"

echo "install: $INSTALL_DIR, build: $BUILD_DIR"

cmake --install-prefix / -D CMAKE_BUILD_TYPE=Debug -S . -B "$BUILD_DIR"

ln -fs -T "$INSTALL_DIR" install
ln -fs -T "$BUILD_DIR" build

cd "$BUILD_DIR" || exit

cmake --build . --verbose
cmake --install . --prefix "$INSTALL_DIR" --verbose

cd "$SRC" || exit

mkdir -p xchg
cp -r -t xchg install/*
