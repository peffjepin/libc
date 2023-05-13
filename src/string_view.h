#ifndef STRING_VIEW_H
#define STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

struct string_view {
    size_t      length;
    const char* data;
};

int  sv_compare(struct string_view, struct string_view);
bool sv_equal(struct string_view, struct string_view);

char sv_lpeek(struct string_view);
char sv_rpeek(struct string_view);

struct string_view sv_lchop(struct string_view*, size_t n);
struct string_view sv_rchop(struct string_view*, size_t n);
char               sv_lchop_char(struct string_view*);
char               sv_rchop_char(struct string_view*);
struct string_view sv_lchop_by_delim(struct string_view*, char c);
struct string_view sv_rchop_by_delim(struct string_view*, char c);

void sv_ldiscard(struct string_view*, size_t n);
void sv_ldiscard_char(struct string_view*);
void sv_rdiscard(struct string_view*, size_t n);
void sv_rdiscard_char(struct string_view*);

void sv_lstrip(struct string_view*);
void sv_rstrip(struct string_view*);
void sv_strip(struct string_view*);

bool sv_starts_with(struct string_view str, struct string_view other);
bool sv_starts_with_cstr(struct string_view str, const char* other);

bool sv_ends_with(struct string_view str, struct string_view other);
bool sv_ends_with_cstr(struct string_view str, const char* other);

bool sv_contains(struct string_view str, struct string_view other);
bool sv_contains_cstr(struct string_view str, const char* other);

char sv_char_at(struct string_view, int index);

#define SV_LITERAL(literal)                                                                        \
    (struct string_view)                                                                           \
    {                                                                                              \
        .length = (literal) ? (sizeof(literal)) - 1 : 0, .data = (literal)                         \
    }

#define SV_CSTR(cstr)                                                                              \
    (struct string_view)                                                                           \
    {                                                                                              \
        .length = strlen(cstr), .data = (cstr)                                                     \
    }

#define SV_FMT "%.*s"
#define SV_ARGS(string_view) (int)(string_view).length, (string_view).data

/*
    struct string_view hello = SV_LITERAL("Hello, world\n");
    printf(SV_FMT, SV_ARGS(hello));
    printf("the string view: " SV_FMT "\n", SV_ARGS(hello));
*/

#ifdef STRING_VIEW_TEST_MAIN

#include <assert.h>
#include <stdio.h>
#define TEST_ASSERT assert

int
main(void)
{
    // comparisons and equality testing
    //

    TEST_ASSERT(sv_equal(SV_LITERAL("abc"), SV_LITERAL("abc")));
    TEST_ASSERT(sv_equal((struct string_view){0}, (struct string_view){0}));
    TEST_ASSERT(sv_equal(SV_LITERAL(""), SV_LITERAL("")));
    TEST_ASSERT(sv_equal((struct string_view){0}, SV_LITERAL("")));
    TEST_ASSERT(!sv_equal(SV_LITERAL("cba"), SV_LITERAL("abc")));
    TEST_ASSERT(!sv_equal(SV_LITERAL("abc"), SV_LITERAL("abcd")));
    TEST_ASSERT(!sv_equal(SV_LITERAL(""), SV_LITERAL("hello2")));
    TEST_ASSERT(!sv_equal((struct string_view){0}, SV_LITERAL("hello2")));

    TEST_ASSERT(sv_compare(SV_LITERAL("abc"), SV_LITERAL("abc")) == 0);
    TEST_ASSERT(sv_compare((struct string_view){0}, (struct string_view){0}) == 0);
    TEST_ASSERT(sv_compare(SV_LITERAL(""), SV_LITERAL("")) == 0);
    TEST_ASSERT(sv_compare((struct string_view){0}, SV_LITERAL("")) == 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("cba"), SV_LITERAL("abc")) != 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("abc"), SV_LITERAL("abcd")) != 0);
    TEST_ASSERT(sv_compare((struct string_view){0}, SV_LITERAL("hello2")) != 0);
    TEST_ASSERT(sv_compare(SV_LITERAL(""), SV_LITERAL("hello2")) != 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("abc"), SV_LITERAL("ab")) > 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("ac"), SV_LITERAL("ab")) > 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("a"), SV_LITERAL("")) > 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("ab"), SV_LITERAL("abc")) < 0);
    TEST_ASSERT(sv_compare(SV_LITERAL("ab"), SV_LITERAL("ac")) < 0);
    TEST_ASSERT(sv_compare(SV_LITERAL(""), SV_LITERAL("a")) < 0);

    // characters by index
    //
    {
        struct string_view str = SV_LITERAL("test");
        TEST_ASSERT(sv_char_at(str, 0) == 't');
        TEST_ASSERT(sv_char_at(str, 1) == 'e');
        TEST_ASSERT(sv_char_at(str, 2) == 's');
        TEST_ASSERT(sv_char_at(str, 3) == 't');
        TEST_ASSERT(sv_char_at(str, -1) == 't');
        TEST_ASSERT(sv_char_at(str, -2) == 's');
        TEST_ASSERT(sv_char_at(str, -3) == 'e');
        TEST_ASSERT(sv_char_at(str, -4) == 't');
    }

    // characters by index (out of bounds)
    //
    {
        struct string_view str = SV_LITERAL("test");
        TEST_ASSERT(sv_char_at(str, 4) == '\0');
        TEST_ASSERT(sv_char_at(str, 100) == '\0');
        TEST_ASSERT(sv_char_at(str, -5) == '\0');
        TEST_ASSERT(sv_char_at(str, -100) == '\0');
    }
    {
        struct string_view empty = SV_LITERAL("");
        TEST_ASSERT(sv_char_at(empty, 0) == '\0');
        struct string_view null = SV_LITERAL(NULL);
        TEST_ASSERT(sv_char_at(null, 0) == '\0');
    }

    // chop left
    //
    {
        struct string_view view = SV_LITERAL("testing");
        struct string_view left = sv_lchop(&view, 4);

        TEST_ASSERT(sv_equal(left, SV_LITERAL("test")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("ing")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        struct string_view left = sv_lchop(&view, 10);

        TEST_ASSERT(sv_equal(left, SV_LITERAL("testing")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        struct string_view left = sv_lchop(&view, 0);

        TEST_ASSERT(sv_equal(left, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("testing")));
    }
    {
        struct string_view empty_view = SV_LITERAL("");
        struct string_view left       = sv_lchop(&empty_view, 1);

        TEST_ASSERT(sv_equal(left, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(empty_view, SV_LITERAL("")));
    }

    // chop right
    //
    {
        struct string_view view  = SV_LITERAL("testing");
        struct string_view right = sv_rchop(&view, 4);

        TEST_ASSERT(sv_equal(right, SV_LITERAL("ting")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("tes")));
    }
    {
        struct string_view view  = SV_LITERAL("testing");
        struct string_view right = sv_rchop(&view, 10);

        TEST_ASSERT(sv_equal(right, SV_LITERAL("testing")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view  = SV_LITERAL("testing");
        struct string_view right = sv_rchop(&view, 0);

        TEST_ASSERT(sv_equal(right, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("testing")));
    }
    {
        struct string_view empty_view = SV_LITERAL("");
        struct string_view right      = sv_rchop(&empty_view, 1);

        TEST_ASSERT(sv_equal(right, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(empty_view, SV_LITERAL("")));
    }


    // chop left by delim
    //
    {
        struct string_view view = SV_LITERAL("hello.world");
        struct string_view left = sv_lchop_by_delim(&view, '.');

        TEST_ASSERT(sv_equal(left, SV_LITERAL("hello")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("world")));
    }
    {
        struct string_view view = SV_LITERAL("hello");
        struct string_view left = sv_lchop_by_delim(&view, '.');

        TEST_ASSERT(sv_equal(left, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hello")));
    }
    {
        struct string_view empty_view = SV_LITERAL("");
        struct string_view left       = sv_lchop_by_delim(&empty_view, '.');
        TEST_ASSERT(sv_equal(left, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(empty_view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("hello,");
        struct string_view left = sv_lchop_by_delim(&view, ',');

        TEST_ASSERT(sv_equal(left, SV_LITERAL("hello")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view  = SV_LITERAL("hello, world, how are you?");
        struct string_view left1 = sv_lchop_by_delim(&view, ',');
        struct string_view left2 = sv_lchop_by_delim(&view, ',');

        TEST_ASSERT(sv_equal(left1, SV_LITERAL("hello")));
        TEST_ASSERT(sv_equal(left2, SV_LITERAL(" world")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL(" how are you?")));
    }

    // chop right by delim
    //
    {
        struct string_view view  = SV_LITERAL("hello.world");
        struct string_view right = sv_rchop_by_delim(&view, '.');

        TEST_ASSERT(sv_equal(right, SV_LITERAL("world")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hello")));
    }
    {
        struct string_view view  = SV_LITERAL("hello");
        struct string_view right = sv_rchop_by_delim(&view, '.');

        TEST_ASSERT(sv_equal(right, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hello")));
    }
    {
        struct string_view empty_view = SV_LITERAL("");
        struct string_view right      = sv_rchop_by_delim(&empty_view, '.');
        TEST_ASSERT(sv_equal(right, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(empty_view, SV_LITERAL("")));
    }
    {
        struct string_view view  = SV_LITERAL("hello,");
        struct string_view right = sv_rchop_by_delim(&view, ',');

        TEST_ASSERT(sv_equal(right, SV_LITERAL("")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hello")));
    }
    {
        struct string_view view   = SV_LITERAL("hello, world, how are you?");
        struct string_view right1 = sv_rchop_by_delim(&view, ',');
        struct string_view right2 = sv_rchop_by_delim(&view, ',');

        TEST_ASSERT(sv_equal(right1, SV_LITERAL(" how are you?")));
        TEST_ASSERT(sv_equal(right2, SV_LITERAL(" world")));
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hello")));
    }

    // chop left char
    //
    {
        struct string_view view       = SV_LITERAL("hello");
        char               first_char = sv_lchop_char(&view);

        TEST_ASSERT(first_char == 'h');
        TEST_ASSERT(sv_equal(view, SV_LITERAL("ello")));
    }
    {
        struct string_view view       = SV_LITERAL("");
        char               first_char = sv_lchop_char(&view);

        TEST_ASSERT(first_char == '\0');
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }

    // chop right char
    //
    {
        struct string_view view      = SV_LITERAL("hello");
        char               last_char = sv_rchop_char(&view);

        TEST_ASSERT(last_char == 'o');
        TEST_ASSERT(sv_equal(view, SV_LITERAL("hell")));
    }
    {
        struct string_view view      = SV_LITERAL("");
        char               last_char = sv_rchop_char(&view);

        TEST_ASSERT(last_char == '\0');
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }

    // peek left char
    //
    {
        struct string_view view       = SV_LITERAL("hello");
        char               first_char = sv_lpeek(view);

        TEST_ASSERT(first_char == 'h');
    }
    {
        struct string_view view       = SV_LITERAL("");
        char               first_char = sv_lpeek(view);

        TEST_ASSERT(first_char == '\0');
    }

    // peek right char
    //
    {
        struct string_view view      = SV_LITERAL("hello");
        char               last_char = sv_rpeek(view);

        TEST_ASSERT(last_char == 'o');
    }
    {
        struct string_view view      = SV_LITERAL("");
        char               last_char = sv_rpeek(view);

        TEST_ASSERT(last_char == '\0');
    }

    // discard left
    //
    {
        struct string_view view = SV_LITERAL("testing");
        sv_ldiscard(&view, 3);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("ting")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        sv_ldiscard(&view, 0);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("testing")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        sv_ldiscard(&view, 10);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("");
        sv_ldiscard(&view, 3);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("test");

        sv_ldiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("est")));
        sv_ldiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("st")));
        sv_ldiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("t")));
        sv_ldiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
        sv_ldiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }

    // discard right
    //
    {
        struct string_view view = SV_LITERAL("testing");
        sv_rdiscard(&view, 3);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("test")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        sv_rdiscard(&view, 0);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("testing")));
    }
    {
        struct string_view view = SV_LITERAL("testing");
        sv_rdiscard(&view, 10);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("");
        sv_rdiscard(&view, 3);

        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }
    {
        struct string_view view = SV_LITERAL("test");

        sv_rdiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("tes")));
        sv_rdiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("te")));
        sv_rdiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("t")));
        sv_rdiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
        sv_rdiscard_char(&view);
        TEST_ASSERT(sv_equal(view, SV_LITERAL("")));
    }

    // strip left
    //
    {
        struct string_view view = SV_LITERAL("   hello");
        sv_lstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("hello   ");
        sv_lstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello   "), view));
    }
    {
        struct string_view view = SV_LITERAL("hello");
        sv_lstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("  ");
        sv_lstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }
    {
        struct string_view view = SV_LITERAL("");
        sv_lstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }

    // strip right
    //
    {
        struct string_view view = SV_LITERAL("   hello");
        sv_rstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("   hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("hello   ");
        sv_rstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("hello");
        sv_rstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("  ");
        sv_rstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }
    {
        struct string_view view = SV_LITERAL("");
        sv_rstrip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }

    // strip
    //
    {
        struct string_view view = SV_LITERAL("   hello");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("hello   ");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("   hello   ");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("hello");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL("hello"), view));
    }
    {
        struct string_view view = SV_LITERAL("  ");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }
    {
        struct string_view view = SV_LITERAL("");
        sv_strip(&view);

        TEST_ASSERT(sv_equal(SV_LITERAL(""), view));
    }

    // starts with
    //
    TEST_ASSERT(sv_starts_with(SV_LITERAL("abcdefg"), SV_LITERAL("abc")));
    TEST_ASSERT(sv_starts_with(SV_LITERAL("abcdefg"), SV_LITERAL("abcdefg")));
    TEST_ASSERT(!sv_starts_with(SV_LITERAL("abcdefg"), SV_LITERAL("def")));
    TEST_ASSERT(!sv_starts_with(SV_LITERAL("abcdefg"), SV_LITERAL("")));
    TEST_ASSERT(!sv_starts_with(SV_LITERAL("abcdefg"), SV_LITERAL("abcdefghi")));
    TEST_ASSERT(!sv_starts_with(SV_LITERAL(""), SV_LITERAL("abcdefghi")));
    TEST_ASSERT(!sv_starts_with(SV_LITERAL(""), SV_LITERAL("")));
    TEST_ASSERT(sv_starts_with_cstr(SV_LITERAL("abcdefg"), "abc"));
    TEST_ASSERT(sv_starts_with_cstr(SV_LITERAL("abcdefg"), "abcdefg"));
    TEST_ASSERT(!sv_starts_with_cstr(SV_LITERAL("abcdefg"), "def"));
    TEST_ASSERT(!sv_starts_with_cstr(SV_LITERAL("abcdefg"), ""));
    TEST_ASSERT(!sv_starts_with_cstr(SV_LITERAL("abcdefg"), "abcdefghi"));
    TEST_ASSERT(!sv_starts_with_cstr(SV_LITERAL(""), "abcdefghi"));
    TEST_ASSERT(!sv_starts_with_cstr(SV_LITERAL(""), ""));

    // ends with
    //
    TEST_ASSERT(sv_ends_with(SV_LITERAL("abcdefg"), SV_LITERAL("efg")));
    TEST_ASSERT(sv_ends_with(SV_LITERAL("abcdefg"), SV_LITERAL("abcdefg")));
    TEST_ASSERT(!sv_ends_with(SV_LITERAL("abcdefg"), SV_LITERAL("def")));
    TEST_ASSERT(!sv_ends_with(SV_LITERAL("abcdefg"), SV_LITERAL("")));
    TEST_ASSERT(!sv_ends_with(SV_LITERAL("abcdefg"), SV_LITERAL("abcdefghi")));
    TEST_ASSERT(!sv_ends_with(SV_LITERAL(""), SV_LITERAL("abcdefghi")));
    TEST_ASSERT(!sv_ends_with(SV_LITERAL(""), SV_LITERAL("")));
    TEST_ASSERT(sv_ends_with_cstr(SV_LITERAL("abcdefg"), "efg"));
    TEST_ASSERT(sv_ends_with_cstr(SV_LITERAL("abcdefg"), "abcdefg"));
    TEST_ASSERT(!sv_ends_with_cstr(SV_LITERAL("abcdefg"), "def"));
    TEST_ASSERT(!sv_ends_with_cstr(SV_LITERAL("abcdefg"), ""));
    TEST_ASSERT(!sv_ends_with_cstr(SV_LITERAL("abcdefg"), "abcdefghi"));
    TEST_ASSERT(!sv_ends_with_cstr(SV_LITERAL(""), "abcdefghi"));
    TEST_ASSERT(!sv_ends_with_cstr(SV_LITERAL(""), ""));

    // contains
    //
    TEST_ASSERT(sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("abc")));
    TEST_ASSERT(sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("efg")));
    TEST_ASSERT(sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("def")));
    TEST_ASSERT(!sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("zzz")));
    TEST_ASSERT(!sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("abcdefgh")));
    TEST_ASSERT(!sv_contains(SV_LITERAL("abcdefg"), SV_LITERAL("")));
    TEST_ASSERT(!sv_contains(SV_LITERAL(""), SV_LITERAL("abcdefghi")));
    TEST_ASSERT(!sv_contains(SV_LITERAL(""), SV_LITERAL("")));
    TEST_ASSERT(sv_contains_cstr(SV_LITERAL("abcdefg"), "abc"));
    TEST_ASSERT(sv_contains_cstr(SV_LITERAL("abcdefg"), "efg"));
    TEST_ASSERT(sv_contains_cstr(SV_LITERAL("abcdefg"), "def"));
    TEST_ASSERT(!sv_contains_cstr(SV_LITERAL("abcdefg"), "zzz"));
    TEST_ASSERT(!sv_contains_cstr(SV_LITERAL("abcdefg"), "abcdefgh"));
    TEST_ASSERT(!sv_contains_cstr(SV_LITERAL("abcdefg"), ""));
    TEST_ASSERT(!sv_contains_cstr(SV_LITERAL(""), "abcdefghi"));
    TEST_ASSERT(!sv_contains_cstr(SV_LITERAL(""), ""));

    printf("%s tests passed\n", __FILE__);
}

#endif  // STRING_VIEW_TEST_MAIN

#endif  // STRING_VIEW_H

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
