#/usr/bin/sh
set -xe

CC=clang
CFLAGS=-Wall -Wextra -Iinclude
LDFLAGS=-L libs -lraylib

if [ ! -f ./build]; then
    mkdir ./build
    cp ./libs/libraylib.so ./build
fi

$CC $CFLAGS -o chip8 ./src/chip8.c $LDFLAGS
