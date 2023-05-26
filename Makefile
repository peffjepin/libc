.PHONY = test test-release clean

CC ?= gcc
EXTRA_FLAGS ?=
FLAGS = -Wall -Wextra -Wpedantic -std=c11 $(EXTRA_FLAGS)
DEBUG_FLAGS = -fsanitize=address -g -fno-omit-frame-pointer
RELEASE_FLAGS = -O3

test:
	mkdir -p build
	$(CC) $(FLAGS) $(DEBUG_FLAGS) -DFILESYSTEM_TEST_MAIN src/filesystem.c -o build/test_filesystem && ./build/test_filesystem
	$(CC) $(FLAGS) $(DEBUG_FLAGS) -DALLOCATOR_TEST_MAIN src/allocator.c -o build/test_allocator && ./build/test_allocator
	$(CC) $(FLAGS) $(DEBUG_FLAGS) -DSTRING_VIEW_TEST_MAIN src/string_view.c -o build/test_string_view && ./build/test_string_view
	$(CC) $(FLAGS) $(DEBUG_FLAGS) -DCLI_TEST_MAIN src/cli.c -o build/test_cli && ./build/test_cli

test-release:
	mkdir -p build
	$(CC) $(FLAGS) $(RELEASE_FLAGS) -DFILESYSTEM_TEST_MAIN src/filesystem.c -o build/test_filesystem && ./build/test_filesystem
	$(CC) $(FLAGS) $(RELEASE_FLAGS) -DALLOCATOR_TEST_MAIN src/allocator.c -o build/test_allocator && ./build/test_allocator
	$(CC) $(FLAGS) $(RELEASE_FLAGS) -DSTRING_VIEW_TEST_MAIN src/string_view.c -o build/test_string_view && ./build/test_string_view
	$(CC) $(FLAGS) $(RELEASE_FLAGS) -DCLI_TEST_MAIN src/cli.c -o build/test_cli && ./build/test_cli

clean:
	rm -rf build
