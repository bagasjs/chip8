#/usr/bin/sh
set -xe

CC=clang
CFLAGS=-Wall -Wextra -Iinclude
LDFLAGS=-L libs -lraylib

$CC $CFLAGS -o chip8 ./src/chip8.c $LDFLAGS
