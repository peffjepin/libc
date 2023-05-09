.PHONY = test

CC ?= gcc
EXTRA_FLAGS ?=
FLAGS = -Wall -Wextra -Wpedantic -std=c11 $(EXTRA_FLAGS)

test:
	mkdir -p build
	$(CC) $(FLAGS) -DFILESYSTEM_TEST_MAIN src/filesystem.c -o build/test_filesystem && ./build/test_filesystem
