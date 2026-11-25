CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs)

TARGET := speedy

all: $(TARGET)

$(TARGET): src/speedy.c src/libretro.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/speedy.c -o $(TARGET) $(SDL_LIBS) -ldl -lm

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
