# LIBC 

This isn't actually libc. This is where the libraries I write and end up reusing between project eventually end up.

The libraries in this repository try to have as few dependencies as possible and follow some common conventions.

## Allocations

Each library should route all of it's allocations through a single library defined function (or small collection of functions).

The internal allocation function should always take an allocator as a parameter which can be customized be the user by modifying
a typedef in the header.

NULL should always be an acceptable value for an allocator and represents the default.

### Example:

```c
// filesystem.h
//

#ifndef FS_DEFAULT_MALLOC
#include <stdlib.h>
#define FS_DEFAULT_MALLOC malloc
#endif

typedef void FilesystemAllocator;

struct fs_content fs_read_file_binary(const char* filepath, FilesystemAllocator*, struct fs_error*);


// filesystem.c
//

void*
fs_malloc(size_t size, FilesystemAllocator* allocator)
{
    if (!allocator) {
        return FS_DEFAULT_MALLOC(size);
    }
    // user code can go here to integrate with custom allocators
    ...
}
```

## Errors

If a library function can raise an error, it must take an out pointer to some error type as it's final parameter.

NULL should always be an acceptable value for the error out param. When NULL is given as the error out param,
the library should write an error message to stderr and exit.

### Example:

```c
// filesystem.h
//

// NOTE: the error type does not necessarily have to be a struct as in this example.
// An enum could also be a good choice.

struct fs_error {
    enum fs_error_code code;
    char reason[128];
};

struct fs_content fs_read_file_binary(const char* filepath, FilesystemAllocator*, struct fs_error*);
```

### Exceptions:

The allocator module does not follow this pattern and instead follows the standard pattern of returning NULL on failure.

## Tests

Tests should be included at the bottom of the header behind a preprocessor guard and should serve as an example
of how to use the library.

### Example:

```c
// filesytem.h
//

...

#ifdef FILESYSTEM_TEST_MAIN

int main(void) 
{
    ...
}

#endif
```

```sh
gcc src/filesystem.c -DFILESYSTEM_TEST_MAIN && ./a.out
```
