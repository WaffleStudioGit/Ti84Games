CC = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc -Ihost
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs) -lSDL2_ttf -lm

.PHONY: all clean
all: spire-host spire-selftest

spire-host: host/main.c host/bot.c src/game.c src/game.h host/bot.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ host/main.c host/bot.c src/game.c $(SDL_LIBS)

spire-selftest: host/selftest.c host/bot.c src/game.c src/game.h host/bot.h
	$(CC) $(CFLAGS) -o $@ host/selftest.c host/bot.c src/game.c -lm

clean:
	rm -f spire-host spire-selftest
