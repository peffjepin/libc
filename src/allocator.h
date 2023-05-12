#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef ALLOCATOR_INTERNAL_MALLOC
#include <stdlib.h>
#define ALLOCATOR_INTERNAL_MALLOC malloc
#endif

#ifndef ALLOCATOR_INTERNAL_REALLOC
#include <stdlib.h>
#define ALLOCATOR_INTERNAL_REALLOC realloc
#endif

#ifndef ALLOCATOR_INTERNAL_FREE
#include <stdlib.h>
#define ALLOCATOR_INTERNAL_FREE free
#endif

#ifndef ALLOCATOR_ASSERT
#include <assert.h>
#define ALLOCATOR_ASSERT assert
#endif

#ifndef ALLOCATOR_STATIC_ASSERT
#include <assert.h>
#define ALLOCATOR_STATIC_ASSERT static_assert
#endif

#ifndef ALLOCATOR_ABORT
#include <stdlib.h>
#include <stdio.h>
#define ALLOCATOR_ABORT(msg)                                                                       \
    do {                                                                                           \
        fprintf(stderr, "ERROR: %s\n", (msg));                                                     \
        abort();                                                                                   \
    } while (0)
#endif

typedef uint64_t AllocatorBlock;

struct allocation {
    uint32_t block_count;
    uint32_t freelist_id;  // freelist index + 1, (0 is reserved for nodes not in the freelist)
    AllocatorBlock blocks[];
};

ALLOCATOR_STATIC_ASSERT(
    sizeof(struct allocation) % sizeof(AllocatorBlock) == 0, "allocation head unaligned"
);

struct allocation_array {
    size_t              capacity;
    size_t              count;
    struct allocation** allocations;
};

struct arena_page {
    AllocatorBlock*         end;
    AllocatorBlock*         head;
    AllocatorBlock*         memory;
    struct allocation_array freelist;
    bool                    owns_memory;
};

struct arena_page arena_page_create_from_memory(void* memory, size_t size, bool page_owns_memory);

struct arena {
    size_t             page_size;
    size_t             page_count;
    struct arena_page* pages;
};

struct allocator {
    struct allocator* fallback;

    enum {
        ALLOCATOR_DEFAULT = 0,
        ALLOCATOR_DEFAULT_PLUS,
        ALLOCATOR_STATIC_ARENA,
        ALLOCATOR_ARENA,
    } type;

    union {
        struct allocation_array default_plus_allocations;
        struct arena_page       static_page;
        struct arena            arena;
    };
};

void* allocator_malloc(struct allocator*, size_t size);
void* allocator_calloc(struct allocator*, size_t count, size_t size);
void* allocator_realloc(struct allocator*, void* ptr, size_t size);
void* allocator_copy(struct allocator*, const void* ptr, size_t size);
void  allocator_free(struct allocator*, void* ptr);
void  allocator_destroy(struct allocator*);

#define _ALLOCATOR_MACROVAR_CONCAT(a, b) a##b
#define _ALLOCATOR_MACROVAR_CONCAT_INDIRECT(a, b) _ALLOCATOR_MACROVAR_CONCAT(a, b)
#define _ALLOCATOR_MACROVAR(name) _ALLOCATOR_MACROVAR_CONCAT_INDIRECT(name, __LINE__)

#define STACK_ALLOCATOR(allocator_variable, capacity)                                              \
    AllocatorBlock _ALLOCATOR_MACROVAR(stack_allocator_memory                                      \
    )[(capacity) / sizeof(AllocatorBlock)];                                                        \
    allocator_variable = (struct allocator)                                                        \
    {                                                                                              \
        .type        = ALLOCATOR_STATIC_ARENA,                                                     \
        .static_page = arena_page_create_from_memory(                                              \
            _ALLOCATOR_MACROVAR(stack_allocator_memory), (capacity), false                         \
        ),                                                                                         \
    }

#define DEFAULT_PLUS_ALLOCATOR(allocator_variable)                                                 \
    (allocator_variable) = (struct allocator){                                                     \
        .type = ALLOCATOR_DEFAULT_PLUS,                                                            \
    };

#define STACK_ALLOCATOR_PLUS(allocator_variable, capacity)                                         \
    STACK_ALLOCATOR(allocator_variable, capacity);                                                 \
    struct allocator _ALLOCATOR_MACROVAR(fallback) = {                                             \
        .type = ALLOCATOR_DEFAULT_PLUS,                                                            \
    };                                                                                             \
    allocator_variable.fallback = &_ALLOCATOR_MACROVAR(fallback)

#define ARENA_ALLOCATOR(allocator_variable, arena_page_size)                                       \
    allocator_variable = (struct allocator)                                                        \
    {                                                                                              \
        .type  = ALLOCATOR_ARENA,                                                                  \
        .arena = {                                                                                 \
            .page_size = (arena_page_size),                                                        \
        },                                                                                         \
    }

#define MALLOC_OR_ELSE(alloc_ptr, result_ptr, size)                                                \
    if (!((result_ptr) = allocator_malloc((alloc_ptr), (size))))

#define MALLOC(alloc_ptr, result_ptr, size)                                                        \
    do {                                                                                           \
        MALLOC_OR_ELSE((alloc_ptr), (result_ptr), (size))                                          \
        {                                                                                          \
            ALLOCATOR_ABORT("out of memory");                                                      \
        }                                                                                          \
    } while (0)

#define CALLOC_OR_ELSE(alloc_ptr, result_ptr, count, size)                                         \
    if (!((result_ptr) = allocator_calloc((alloc_ptr), (count), (size))))

#define CALLOC(alloc_ptr, result_ptr, count, size)                                                 \
    do {                                                                                           \
        CALLOC_OR_ELSE((alloc_ptr), (result_ptr), (count), (size))                                 \
        {                                                                                          \
            ALLOCATOR_ABORT("out of memory");                                                      \
        }                                                                                          \
    } while (0)

#define REALLOC_OR_ELSE(alloc_ptr, result_ptr, size)                                               \
    void* _ALLOCATOR_MACROVAR(realloc_tmp);                                                        \
    _ALLOCATOR_MACROVAR(realloc_tmp) = allocator_realloc((alloc_ptr), (result_ptr), (size));       \
    if (_ALLOCATOR_MACROVAR(realloc_tmp)) {                                                        \
        (result_ptr) = _ALLOCATOR_MACROVAR(realloc_tmp);                                           \
    }                                                                                              \
    else

#define REALLOC(alloc_ptr, result_ptr, size)                                                       \
    do {                                                                                           \
        REALLOC_OR_ELSE((alloc_ptr), (result_ptr), (size))                                         \
        {                                                                                          \
            ALLOCATOR_ABORT("out of memory");                                                      \
        }                                                                                          \
    } while (0)

#define COPY_OR_ELSE(alloc_ptr, result_ptr, src_ptr, size)                                         \
    if (!((result_ptr) = allocator_copy((alloc_ptr), (src_ptr), (size))))

#define COPY(alloc_ptr, result_ptr, src_ptr, size)                                                 \
    do {                                                                                           \
        COPY_OR_ELSE(alloc_ptr, result_ptr, src_ptr, size)                                         \
        {                                                                                          \
            ALLOCATOR_ABORT("out of memory");                                                      \
        }                                                                                          \
    } while (0)

#define FREE(alloc_ptr, result_ptr)                                                                \
    do {                                                                                           \
        allocator_free((alloc_ptr), (result_ptr));                                                 \
        (result_ptr) = NULL;                                                                       \
    } while (0)

#define DMALLOC_OR_ELSE(result_ptr, size) MALLOC_OR_ELSE(NULL, (result_ptr), (size))
#define DMALLOC(result_ptr, size) MALLOC(NULL, (result_ptr), (size))
#define DCALLOC_OR_ELSE(result_ptr, count) CALLOC_OR_ELSE(NULL, (result_ptr), (count))
#define DCALLOC(result_ptr, count) CALLOC(NULL, (result_ptr), (count))
#define DREALLOC_OR_ELSE(result_ptr, size) REALLOC_OR_ELSE(NULL, (result_ptr), (size))
#define DREALLOC(result_ptr, size) REALLOC(NULL, (result_ptr), (size))
#define DFREE(result_ptr) FREE(NULL, result_ptr)

#ifdef ALLOCATOR_TEST_MAIN

#include <assert.h>
#include <stdio.h>
#define TEST_ASSERT assert

// TODO: these tests could be a lot better

struct int_array {
    size_t count;
    int    data[];
};

struct int_array*
allocate_array(struct allocator* alloc, int fill, size_t size)
{
    struct int_array* array;
    MALLOC(alloc, array, sizeof *array + sizeof(int) * size);
    array->count = size;
    for (size_t i = 0; i < size; i++) {
        array->data[i] = fill;
    }
    return array;
}

struct int_array*
reallocate_array(struct allocator* alloc, struct int_array* array, size_t size)
{
    REALLOC(alloc, array, sizeof *array + sizeof(int) * size);
    array->count = size;
    for (size_t i = 1; i < size; i++) {
        array->data[i] = array->data[0];
    }
    return array;
}

size_t
random_index(size_t array_length)
{
    return rand() % array_length;
}

#define CHOICE(array) ((array)[random_index(sizeof(array) / sizeof *(array))])

int
main(void)
{
    unsigned int seed = 0;
    srand(seed);

    // stack allocator
    //
    {
        struct allocator alloc;
        STACK_ALLOCATOR(alloc, 450);

        size_t i = 0;
        while (i++ < 10) {
            void* mem;
            MALLOC_OR_ELSE(&alloc, mem, 100)
            {
                break;
            }
        }
        TEST_ASSERT(i == 4);
    }

    // stack allocator plus (falls back to default allocator after running out of space)
    //
    {
        struct allocator stackp;
        STACK_ALLOCATOR_PLUS(stackp, 500);

        size_t i = 0;

        while (i++ < 20) {
            void* mem;
            MALLOC_OR_ELSE(&stackp, mem, 120)
            {
                TEST_ASSERT(0 && "should not fail");
            }
        }

        allocator_destroy(&stackp);
    }

    // stress test
    //
    {
        struct allocator alloc = {
            .type = ALLOCATOR_ARENA,
            .arena =
                {
                    .page_size = 1024 * 1024,
                },
        };

#define ARRAY_COUNT 4096
        struct int_array* arrays[ARRAY_COUNT];

        const size_t size_table[] = {1,  2,  3,  4,  5,  8,   10,  11,  12,  13,  16,
                                     24, 27, 32, 64, 90, 100, 112, 512, 600, 1024};

        for (int i = 0; i < ARRAY_COUNT; i++) {
            arrays[i] = allocate_array(&alloc, i, CHOICE(size_table));
        }

        for (size_t i = 0; i < 10000; i++) {
            arrays[i % ARRAY_COUNT] =
                reallocate_array(&alloc, arrays[i % ARRAY_COUNT], CHOICE(size_table));
        }

        for (int i = 0; i < ARRAY_COUNT; i++) {
            struct int_array* array = arrays[i];
            for (size_t j = 0; j < array->count; j++) {
                TEST_ASSERT(array->data[j] == i);
            }
        }

        allocator_destroy(&alloc);
    }

    printf("%s tests passed\n", __FILE__);
}

#endif  // ALLOCATOR_TEST_MAIN

#endif  // ALLOCATOR_H

/*
==============================================================================
OPTION 1 (MIT)
==============================================================================

Copyright (c) 2023, Jeffrey Pepin.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


==============================================================================
OPTION 2 (Public Domain)
==============================================================================

This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
