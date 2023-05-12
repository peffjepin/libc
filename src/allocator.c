#include "allocator.h"

#include <string.h>

static const size_t ALLOCATION_HEAD_BLOCK_COUNT =
    sizeof(struct allocation) / sizeof(AllocatorBlock);
static const size_t MIN_BLOCKS_REQUIRED_FOR_ALLOCATION = 1 + ALLOCATION_HEAD_BLOCK_COUNT;

struct allocator default_allocator = {
    .type = ALLOCATOR_DEFAULT,
};

static size_t
blocks_required_for_size(size_t size)
{
    return ((size + sizeof(AllocatorBlock) - 1) / sizeof(AllocatorBlock));
}

static size_t
aligned_data_size(size_t data_size)
{
    return blocks_required_for_size(data_size) * sizeof(AllocatorBlock);
}

static size_t
total_allocation_size_by_data_size(size_t data_size)
{
    return sizeof(struct allocation) + aligned_data_size(data_size);
}

static size_t
allocation_get_actual_data_size(const struct allocation* a)
{
    ALLOCATOR_ASSERT(a);
    return a->block_count * (sizeof *a->blocks);
}

static struct allocation*
allocation_view_from_application_pointer(void* ptr)
{
    ALLOCATOR_ASSERT(ptr);

    uint8_t* bytes_view = ptr;
    return (struct allocation*)(bytes_view - sizeof(struct allocation));
}

static struct allocation*
allocation_next(const struct allocation* mem)
{
    ALLOCATOR_ASSERT(mem);
    return (struct allocation*)(mem->blocks + mem->block_count);
}

static bool
allocation_array_contains(const struct allocation_array* array, const struct allocation* a)
{
    ALLOCATOR_ASSERT(array);
    ALLOCATOR_ASSERT(a);

    if (a->freelist_id == 0 || a->freelist_id > array->count) {
        return false;
    }

    return (array->allocations[a->freelist_id - 1] == a);
}

static void
allocation_array_append(struct allocation_array* array, struct allocation* a)
{
    ALLOCATOR_ASSERT(array);
    ALLOCATOR_ASSERT(a);

    if (array->count >= array->capacity) {
        array->capacity    = 1 + 2 * array->count;
        array->allocations = ALLOCATOR_INTERNAL_REALLOC(
            array->allocations, sizeof *array->allocations * array->capacity
        );
        if (!array->allocations) {
            ALLOCATOR_ABORT("failed to allocate memory for allocator freelist");
        }
    }
    array->allocations[array->count++] = a;
    a->freelist_id                     = array->count;
}

static void
allocation_array_remove(struct allocation_array* array, struct allocation* a)
{
    ALLOCATOR_ASSERT(array);
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(allocation_array_contains(array, a));

    array->allocations[a->freelist_id - 1]              = array->allocations[--(array->count)];
    array->allocations[a->freelist_id - 1]->freelist_id = a->freelist_id;

    a->freelist_id = 0;

    if (array->count * 4 <= array->capacity) {
        array->capacity    = 1 + 2 * array->count;
        array->allocations = ALLOCATOR_INTERNAL_REALLOC(
            array->allocations, sizeof &array->allocations * array->capacity
        );
        if (!array->allocations) {
            ALLOCATOR_ABORT("failed to allocate memory for allocator freelist");
        }
    }
}

// returns 0 on failure otherwise returns the number of blocks the caller
// has taken ownership of (caller may acquire more blocks than requested)
//
static size_t
allocation_array_try_to_take_blocks_from_member(
    struct allocation_array* array, struct allocation* member, size_t required_blocks
)
{
    ALLOCATOR_ASSERT(array);
    ALLOCATOR_ASSERT(member);
    ALLOCATOR_ASSERT(allocation_array_contains(array, member));

    const size_t available_blocks = member->block_count + ALLOCATION_HEAD_BLOCK_COUNT;

    // Not enough blocks available.
    //
    if (available_blocks < required_blocks) {
        return 0;
    }

    // Enough blocks available, but not enough surplus to support splitting the
    // allocation into two smaller allocations.
    //
    if (available_blocks < required_blocks + MIN_BLOCKS_REQUIRED_FOR_ALLOCATION) {
        allocation_array_remove(array, member);
        return available_blocks;
    }

    // Enough blocks available, and enough surplus to support splitting the allocation into
    // two smaller allocations.
    //
    const size_t remaining_blocks               = available_blocks - required_blocks;
    member->block_count                         = required_blocks - ALLOCATION_HEAD_BLOCK_COUNT;
    struct allocation* new_free_node            = allocation_next(member);
    new_free_node->block_count                  = remaining_blocks - ALLOCATION_HEAD_BLOCK_COUNT;
    new_free_node->freelist_id                  = member->freelist_id;
    array->allocations[member->freelist_id - 1] = new_free_node;
    return required_blocks;
}

// If the given allocation is adjacent to another allocation already in the array then the two
// will be joined into a single larger allocation. Otherwise the given allocation will be
// appended to the array.
//
static void
allocation_array_join_allocation(struct allocation_array* array, struct allocation* a)
{
    ALLOCATOR_ASSERT(array);
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(a->freelist_id == 0);

    // try to merge with adjecent node to the right
    //
    struct allocation* next = allocation_next(a);
    if (allocation_array_contains(array, next)) {
        a->freelist_id = next->freelist_id;
        a->block_count += next->block_count + ALLOCATION_HEAD_BLOCK_COUNT;
        array->allocations[a->freelist_id - 1] = a;
    }

    // try to merge with adjacent node to the left
    //
    for (size_t i = 0; i < array->count; i++) {
        struct allocation* before                   = array->allocations[i];
        struct allocation* next_after_iterated_node = allocation_next(before);
        if (next_after_iterated_node == a) {
            before->block_count += a->block_count + ALLOCATION_HEAD_BLOCK_COUNT;
            if (a->freelist_id != 0) {
                allocation_array_remove(array, a);
            }
            return;
        }
    }

    // no adjecent node in array to merge with
    //
    if (a->freelist_id == 0) {
        allocation_array_append(array, a);
    }
}

struct arena_page
arena_page_create_from_memory(void* memory, size_t size, bool page_owns_memory)
{
    AllocatorBlock* block_view  = memory;
    const size_t    block_count = size / sizeof *block_view;

    if (block_count < ALLOCATION_HEAD_BLOCK_COUNT) {
        ALLOCATOR_ABORT("trying to initialize an arena page with too few blocks");
    }

    struct arena_page page = {
        .memory      = block_view,
        .head        = block_view,
        .end         = block_view + block_count - ALLOCATION_HEAD_BLOCK_COUNT,
        .owns_memory = page_owns_memory,
    };

    // memset the last slot to 0 so that `allocation_next` will never return
    // a view into uninitialized memory.
    //
    struct allocation* end_allocation_view = (struct allocation*)page.end;
    memset(end_allocation_view, 0, sizeof *end_allocation_view);

    return page;
}

static bool
arena_page_contains_allocation(const struct arena_page* page, const struct allocation* a)
{
    ALLOCATOR_ASSERT(page);
    ALLOCATOR_ASSERT(a);

    AllocatorBlock* blockview = (AllocatorBlock*)a;
    return (blockview >= page->memory && blockview < page->end);
}

static void
arena_page_deallocate_entire_page(struct arena_page* page)
{
    if (!page) return;
    if (page->owns_memory) {
        ALLOCATOR_INTERNAL_FREE(page->memory);
    }
    ALLOCATOR_INTERNAL_FREE(page->freelist.allocations);
    *page = (struct arena_page){0};
}

static bool
arena_page_try_advancing_head(struct arena_page* page, size_t advance_block_count)
{
    AllocatorBlock* proposed_head = page->head + advance_block_count;
    if (proposed_head > page->end) return false;
    page->head = proposed_head;
    return true;
}

static bool
arena_page_try_reallocating_in_place(struct arena_page* page, struct allocation* a, size_t size)
{
    ALLOCATOR_ASSERT(page);
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(size);

    const size_t required_blocks = blocks_required_for_size(size);

    // allocation is shrinking
    //
    if (a->block_count >= required_blocks + MIN_BLOCKS_REQUIRED_FOR_ALLOCATION) {
        size_t remaining_blocks = a->block_count - required_blocks;
        ALLOCATOR_ASSERT(remaining_blocks >= MIN_BLOCKS_REQUIRED_FOR_ALLOCATION);

        if ((AllocatorBlock*)allocation_next(a) == page->head) {
            page->head -= remaining_blocks;
            a->block_count = required_blocks;
            return true;
        }

        a->block_count               = required_blocks;
        struct allocation* remainder = allocation_next(a);
        remainder->block_count       = remaining_blocks - ALLOCATION_HEAD_BLOCK_COUNT;
        remainder->freelist_id       = 0;
        allocation_array_join_allocation(&page->freelist, remainder);
        return true;
    }
    // allocation is growing
    //
    else if (a->block_count < required_blocks) {
        const size_t       additional_blocks_required = required_blocks - a->block_count;
        struct allocation* next                       = allocation_next(a);

        // try to allocate extra space from the page head
        //
        if ((AllocatorBlock*)next == page->head) {
            if (!arena_page_try_advancing_head(page, additional_blocks_required)) {
                return false;
            }
            a->block_count += additional_blocks_required;
            return true;
        }

        // try to allocate extra space from a freed allocation
        //
        if (allocation_array_contains(&page->freelist, next)) {
            size_t blocks_allocated = allocation_array_try_to_take_blocks_from_member(
                &page->freelist, next, additional_blocks_required
            );
            if (blocks_allocated == 0) {
                return false;
            }
            ALLOCATOR_ASSERT(blocks_allocated >= additional_blocks_required);
            a->block_count += blocks_allocated;
            return true;
        }

        // allocation could not grow in place
        //
        return false;
    }
    // allocation is unchanged
    //
    return true;
}

static struct allocation*
arena_page_make_allocation(struct arena_page* page, size_t size)
{
    ALLOCATOR_ASSERT(size);
    ALLOCATOR_ASSERT(page);

    const size_t required_blocks = blocks_required_for_size(size) + ALLOCATION_HEAD_BLOCK_COUNT;

    // page full
    //
    if (page->end - page->head <= (int64_t)required_blocks && page->freelist.count == 0) {
        return NULL;
    }

    // attempt to allocate from freelist
    //
    for (size_t i = 0; i < page->freelist.count; i++) {
        struct allocation* a = page->freelist.allocations[i];
        ALLOCATOR_ASSERT(a);

        size_t allocated_blocks =
            allocation_array_try_to_take_blocks_from_member(&page->freelist, a, required_blocks);
        if (allocated_blocks == 0) {
            continue;
        }
        ALLOCATOR_ASSERT(allocated_blocks >= required_blocks);
        a->freelist_id = 0;
        a->block_count = allocated_blocks - ALLOCATION_HEAD_BLOCK_COUNT;
        return a;
    }

    // attempt to allocate from head
    //
    struct allocation* new_allocation = (struct allocation*)page->head;
    if (arena_page_try_advancing_head(page, required_blocks)) {
        new_allocation->block_count = required_blocks - ALLOCATION_HEAD_BLOCK_COUNT;
        new_allocation->freelist_id = 0;
        return new_allocation;
    }

    return NULL;
}

// caller is responsible for ensuring the pointer belongs to this page
//
static void
arena_page_free_allocation(struct arena_page* page, struct allocation* a)
{
    ALLOCATOR_ASSERT(page);
    ALLOCATOR_ASSERT(a);

    struct allocation* next_alloc = allocation_next(a);

    if ((AllocatorBlock*)next_alloc == page->head) {
        page->head -= ALLOCATION_HEAD_BLOCK_COUNT + a->block_count;
    }
    else {
        allocation_array_join_allocation(&page->freelist, a);
    }
}

static const uint32_t DEFAULT_ALLOCATOR_SPECIAL_FREELIST_ID = 0xFFFFFFFF;

static struct allocation*
default_malloc(size_t size)
{
    ALLOCATOR_ASSERT(size);

    struct allocation* a = ALLOCATOR_INTERNAL_MALLOC(total_allocation_size_by_data_size(size));
    if (!a) {
        return NULL;
    }
    a->block_count = blocks_required_for_size(size);
    a->freelist_id = DEFAULT_ALLOCATOR_SPECIAL_FREELIST_ID;
    return a;
}

static struct allocation*
default_realloc(struct allocation* a, size_t size)
{
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(size);
    ALLOCATOR_ASSERT(a->freelist_id == DEFAULT_ALLOCATOR_SPECIAL_FREELIST_ID);

    struct allocation* new =
        ALLOCATOR_INTERNAL_REALLOC(a, total_allocation_size_by_data_size(size));
    if (!new) {
        return NULL;
    }
    new->block_count = blocks_required_for_size(size);
    return new;
}

static void
default_free(struct allocation* a)
{
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(a->freelist_id == DEFAULT_ALLOCATOR_SPECIAL_FREELIST_ID);

    ALLOCATOR_INTERNAL_FREE(a);
}

static struct allocation*
default_plus_malloc(struct allocation_array* default_plus_allocations, size_t size)
{
    ALLOCATOR_ASSERT(default_plus_allocations);
    ALLOCATOR_ASSERT(size);

    const size_t       block_count = blocks_required_for_size(size);
    const size_t       total_size  = total_allocation_size_by_data_size(size);
    struct allocation* a           = ALLOCATOR_INTERNAL_MALLOC(total_size);
    if (!a) {
        return NULL;
    }
    a->block_count = block_count;
    allocation_array_append(default_plus_allocations, a);
    return a;
}

static void
default_plus_free(struct allocation_array* default_plus_allocations, struct allocation* a)
{
    ALLOCATOR_ASSERT(default_plus_allocations);
    ALLOCATOR_ASSERT(a);

    allocation_array_remove(default_plus_allocations, a);
    ALLOCATOR_INTERNAL_FREE(a);
}

// caller is responsible for ensuring the allocation belongs to this allocator
//
static struct allocation*
default_plus_realloc(
    struct allocation_array* default_plus_allocations, struct allocation* a, size_t size
)
{
    ALLOCATOR_ASSERT(default_plus_allocations);
    ALLOCATOR_ASSERT(size);
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(allocation_array_contains(default_plus_allocations, a));

    struct allocation* new =
        ALLOCATOR_INTERNAL_REALLOC(a, total_allocation_size_by_data_size(size));
    if (!new) {
        return NULL;
    }
    if (new != a) {
        default_plus_allocations->allocations[new->freelist_id - 1] = new;
    }
    new->block_count = blocks_required_for_size(size);
    return new;
}

static struct allocation*
arena_malloc(struct arena* arena, size_t size)
{
    ALLOCATOR_ASSERT(arena);
    ALLOCATOR_ASSERT(size);

    if (size > arena->page_size) {
        return NULL;
    }

    struct allocation* a = NULL;
    for (size_t i = 0; i < arena->page_count; i++) {
        if ((a = arena_page_make_allocation(&arena->pages[i], size))) {
            return a;
        }
    }

    arena->page_count += 1;
    arena->pages =
        ALLOCATOR_INTERNAL_REALLOC(arena->pages, sizeof *arena->pages * arena->page_count);
    if (!arena->pages) {
        ALLOCATOR_ABORT("paged allocator failed to allocate page");
    }

    AllocatorBlock* page_memory = ALLOCATOR_INTERNAL_MALLOC(arena->page_size);
    if (!page_memory) {
        ALLOCATOR_ABORT("paged allocator failed to allocate page");
    }

    struct arena_page* new_page = &arena->pages[arena->page_count - 1];
    *new_page = arena_page_create_from_memory(page_memory, arena->page_size, true);
    return arena_page_make_allocation(new_page, size);
}

// caller is responsible for ensuring the allocation belongs to this arena
//
static struct allocation*
arena_realloc(struct arena* arena, struct allocation* a, size_t size)
{
    ALLOCATOR_ASSERT(arena);
    ALLOCATOR_ASSERT(size);
    ALLOCATOR_ASSERT(a);

    if (size > arena->page_size) {
        return NULL;
    }

    struct arena_page* owning_page       = NULL;
    size_t             owning_page_index = 0;

    for (size_t i = 0; i < arena->page_count; i++) {
        if (arena_page_contains_allocation(&arena->pages[i], a)) {
            owning_page       = &arena->pages[i];
            owning_page_index = i;
            break;
        };
    }
    if (!owning_page) {
        return NULL;
    }
    if (arena_page_try_reallocating_in_place(owning_page, a, size)) {
        return a;
    }

    struct allocation* new = arena_malloc(
        arena, size
    );  // may reallocate arena->pages (`owning_page` has become invalid)
    if (!new) {
        return NULL;
    }

    const size_t smaller_block_count =
        (a->block_count < new->block_count) ? a->block_count : new->block_count;
    memcpy(new->blocks, a->blocks, smaller_block_count * sizeof *a->blocks);
    arena_page_free_allocation(&arena->pages[owning_page_index], a);
    return new;
}

// caller is responsible for ensuring the allocation belongs to this page
//
static struct allocation*
static_arena_realloc(struct arena_page* page, struct allocation* a, size_t size)
{
    ALLOCATOR_ASSERT(page);
    ALLOCATOR_ASSERT(a);
    ALLOCATOR_ASSERT(size);

    if (arena_page_try_reallocating_in_place(page, a, size)) {
        return a;
    }

    struct allocation* new = arena_page_make_allocation(page, size);
    if (!new) {
        return NULL;
    }

    const size_t smaller_block_count =
        (a->block_count < new->block_count) ? a->block_count : new->block_count;
    memcpy(new->blocks, a->blocks, (sizeof *new->blocks) * smaller_block_count);
    arena_page_free_allocation(page, a);
    return new;
}

static bool
allocator_owns_memory(struct allocator* allocator, struct allocation* a)
{
    if (!allocator || !a) {
        return false;
    }

    switch (allocator->type) {
        case ALLOCATOR_DEFAULT:
            return a->freelist_id == DEFAULT_ALLOCATOR_SPECIAL_FREELIST_ID;
        case ALLOCATOR_DEFAULT_PLUS:
            return allocation_array_contains(&allocator->default_plus_allocations, a);
        case ALLOCATOR_ARENA:
            for (size_t i = 0; i < allocator->arena.page_count; i++) {
                if (arena_page_contains_allocation(&allocator->arena.pages[i], a)) {
                    return true;
                }
            }
            return false;
        case ALLOCATOR_STATIC_ARENA:
            return arena_page_contains_allocation(&allocator->static_page, a);
    }

    ALLOCATOR_ASSERT(0 && "unreachable");
}

static struct allocator*
find_owning_allocator(struct allocator* root, struct allocation* a)
{
    ALLOCATOR_ASSERT(root);
    ALLOCATOR_ASSERT(a);

    struct allocator* current = root;
    while (current != NULL) {
        if (allocator_owns_memory(current, a)) {
            return current;
        }
        current = current->fallback;
    }
    return NULL;
}

void*
allocator_malloc(struct allocator* allocator, size_t size)
{
    if (!size) {
        return NULL;
    }
    if (!allocator) {
        allocator = &default_allocator;
    }

    struct allocation* a = NULL;

    switch (allocator->type) {
        case ALLOCATOR_DEFAULT: {
            a = default_malloc(size);
            break;
        }
        case ALLOCATOR_DEFAULT_PLUS: {
            a = default_plus_malloc(&allocator->default_plus_allocations, size);
            break;
        }
        case ALLOCATOR_STATIC_ARENA: {
            a = arena_page_make_allocation(&allocator->static_page, size);
            break;
        }
        case ALLOCATOR_ARENA: {
            a = arena_malloc(&allocator->arena, size);
            break;
        }
    }

    if (a) {
        return a->blocks;
    }
    else if (allocator->fallback) {
        return allocator_malloc(allocator->fallback, size);
    }
    return NULL;
}

void*
allocator_calloc(struct allocator* allocator, size_t count, size_t size)
{
    if (!allocator) {
        allocator = &default_allocator;
    }
    const size_t total_size = size * count;
    void*        ptr        = allocator_malloc(allocator, total_size);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, total_size);
    return ptr;
}

void*
allocator_copy(struct allocator* allocator, const void* ptr, size_t size)
{
    if (!allocator) {
        allocator = &default_allocator;
    }
    if (!ptr || !size) {
        return NULL;
    }
    void* a = allocator_malloc(allocator, size);
    if (!a) {
        return NULL;
    }
    memcpy(a, ptr, size);
    return a;
}

// assumes caller has validated that this memory is owned by this allocator
//
static void
allocator_free_internal(struct allocator* allocator, struct allocation* a)
{
    if (!allocator) {
        allocator = &default_allocator;
    }

    switch (allocator->type) {
        case ALLOCATOR_DEFAULT:
            default_free(a);
            return;
        case ALLOCATOR_DEFAULT_PLUS:
            default_plus_free(&allocator->default_plus_allocations, a);
            return;
        case ALLOCATOR_STATIC_ARENA:
            arena_page_free_allocation(&allocator->static_page, a);
            return;
        case ALLOCATOR_ARENA:
            for (size_t i = 0; i < allocator->arena.page_count; i++) {
                if (arena_page_contains_allocation(&allocator->arena.pages[i], a)) {
                    arena_page_free_allocation(&allocator->arena.pages[i], a);
                    return;
                }
            }
            return;
    }
    ALLOCATOR_ASSERT(0 && "unreachable");
}

void
allocator_free(struct allocator* allocator, void* ptr)
{
    if (!ptr) {
        return;
    }
    if (!allocator) {
        allocator = &default_allocator;
    }

    struct allocation* a                = allocation_view_from_application_pointer(ptr);
    struct allocator*  owning_allocator = find_owning_allocator(allocator, a);
    if (!owning_allocator) {
        ALLOCATOR_ABORT("trying to free unrecognized pointer");
    }
    allocator_free_internal(owning_allocator, a);
}

void*
allocator_realloc(struct allocator* allocator, void* ptr, size_t size)
{
    if (!allocator) {
        allocator = &default_allocator;
    }
    if (!size) {
        allocator_free(allocator, ptr);
        return NULL;
    }
    if (!ptr) {
        return allocator_malloc(allocator, size);
    }

    struct allocation* a                = allocation_view_from_application_pointer(ptr);
    struct allocator*  owning_allocator = find_owning_allocator(allocator, a);
    if (!owning_allocator) {
        ALLOCATOR_ABORT("passing unknown pointer to allocator for reallocation");
    }

    struct allocation* result = NULL;

    switch (owning_allocator->type) {
        case ALLOCATOR_DEFAULT: {
            result = default_realloc(a, size);
            break;
        }
        case ALLOCATOR_DEFAULT_PLUS: {
            result = default_plus_realloc(&owning_allocator->default_plus_allocations, a, size);
            break;
        }
        case ALLOCATOR_STATIC_ARENA: {
            result = static_arena_realloc(&owning_allocator->static_page, a, size);
            break;
        }
        case ALLOCATOR_ARENA: {
            result = arena_realloc(&owning_allocator->arena, a, size);
            break;
        }
    }

    if (result) {
        return result->blocks;
    }

    // failed to reallocate, but we can try making a fresh allocation from the root allocator
    //
    void* new = allocator_malloc(allocator, size);
    if (!new) {
        return NULL;
    }
    const size_t mem_data_size = allocation_get_actual_data_size(a);
    memcpy(new, a->blocks, (mem_data_size < size) ? mem_data_size : size);
    allocator_free_internal(owning_allocator, a);
    return new;
}

void
allocator_destroy(struct allocator* allocator)
{
    if (!allocator) {
        return;
    }

    allocator_destroy(allocator->fallback);

    switch (allocator->type) {
        case ALLOCATOR_DEFAULT:
            ALLOCATOR_ABORT("default allocator cannot be destroyed");
        case ALLOCATOR_DEFAULT_PLUS:
            for (size_t i = 0; i < allocator->default_plus_allocations.count; i++) {
                ALLOCATOR_INTERNAL_FREE(allocator->default_plus_allocations.allocations[i]);
            }
            ALLOCATOR_INTERNAL_FREE(allocator->default_plus_allocations.allocations);
            allocator->default_plus_allocations = (struct allocation_array){0};
            return;
        case ALLOCATOR_STATIC_ARENA:
            arena_page_deallocate_entire_page(&allocator->static_page);
            allocator->static_page = (struct arena_page){0};
            return;
        case ALLOCATOR_ARENA:
            for (size_t i = 0; i < allocator->arena.page_count; i++) {
                arena_page_deallocate_entire_page(&allocator->arena.pages[i]);
            }
            ALLOCATOR_INTERNAL_FREE(allocator->arena.pages);
            allocator->arena = (struct arena){0};
            return;
    }

    ALLOCATOR_ASSERT(0 && "unreachable");
}

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
