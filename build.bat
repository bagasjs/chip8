@echo off

set TARGET=chip8
set CC=clang
set CFLAGS=-Wall -Wextra -Iinclude
set LDFLAGS=-Llibs -lraylibdll -lkernel32 -lopengl32 -luser32

if not exist .\build (
    mkdir .\build
    move .\libs\raylib.dll .\build
)

%CC% %CFLAGS% -o .\build\%TARGET%.exe .\src\chip8.c %LDFLAGS%
