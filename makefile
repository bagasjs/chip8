CFLAGS := -Wall -Wextra -ggdb
LDFLAGS := `pkg-config --libs sdl2`

all: build build/chip8-emu
	
build/chip8-emu: ./src/main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build:
	mkdir $@
