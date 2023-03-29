CFLAGS=-std=c17 -Wall -Wextra -Werror -pedantic

all:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs` -g -DDEBUG

run: all
	./chip8 roms/BC_test.ch8
