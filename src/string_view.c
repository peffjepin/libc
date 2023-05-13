#include "string_view.h"

#include <string.h>
#include <ctype.h>

int
sv_compare(struct string_view s1, struct string_view s2)
{
    if (s1.length == 0) {
        if (s2.length == 0) {
            return 0;
        }
        return -1;
    }
    if (s2.length == 0) {
        if (s1.length == 0) {
            return 0;
        }
        return 1;
    }

    size_t smaller_length = (s1.length < s2.length) ? s1.length : s2.length;

    int comparison = strncmp(s1.data, s2.data, smaller_length);
    if (comparison == 0) {
        if (s1.length == s2.length) {
            return 0;
        }
        if (s1.length > s2.length) {
            return 1;
        }
        return -1;
    }
    return comparison;
}

bool
sv_equal(struct string_view s1, struct string_view s2)
{
    if (s1.length != s2.length) {
        return false;
    }
    if (s1.length == 0) {
        return true;
    }
    return strncmp(s1.data, s2.data, s1.length) == 0;
}

struct string_view
sv_lchop(struct string_view* str, size_t n)
{
    if (n >= str->length) {
        struct string_view left = *str;
        str->data += str->length;
        str->length = 0;
        return left;
    }

    struct string_view left = {
        .length = n,
        .data   = str->data,
    };
    str->data += n;
    str->length -= n;
    return left;
}

struct string_view
sv_rchop(struct string_view* str, size_t n)
{
    if (n >= str->length) {
        struct string_view right = *str;
        str->length              = 0;
        return right;
    }

    struct string_view right = {
        .length = n,
        .data   = str->data + str->length - n,
    };
    str->length -= n;
    return right;
}

struct string_view
sv_lchop_by_delim(struct string_view* str, char c)
{
    char* position = memchr(str->data, c, str->length);
    if (!position) {
        return (struct string_view){0};
    }
    size_t length = (size_t)(position - str->data);

    struct string_view left = {
        .length = length,
        .data   = str->data,
    };
    str->data += length + 1;
    str->length -= length + 1;
    return left;
}

static const char*
memrchr(const char* str, char c, size_t n)
{
    if (!str || !n) {
        return NULL;
    }
    const char* scan = str + n - 1;
    while (scan > str) {
        const char current = *scan;
        if (current == c) {
            return scan;
        }
        scan -= 1;
    }
    return NULL;
}

struct string_view
sv_rchop_by_delim(struct string_view* str, char c)
{
    const char* position = memrchr(str->data, c, str->length);
    if (!position) {
        return (struct string_view){0};
    }
    size_t offset = (size_t)(position - str->data);

    struct string_view right = {
        .length = str->length - (offset + 1),
        .data   = str->data + offset + 1,
    };
    str->length = offset;
    return right;
}

char
sv_lchop_char(struct string_view* str)
{
    if (!str || str->length == 0) {
        return '\0';
    }

    char first_char = str->data[0];
    str->data += 1;
    str->length -= 1;

    return first_char;
}

char
sv_rchop_char(struct string_view* str)
{
    if (!str || str->length == 0) {
        return '\0';
    }

    char last_char = str->data[str->length - 1];
    str->length -= 1;

    return last_char;
}

char
sv_lpeek(struct string_view str)
{
    if (str.data && str.length > 0) {
        return str.data[0];
    }
    return '\0';
}

char
sv_rpeek(struct string_view str)
{
    if (str.data && str.length) {
        return str.data[str.length - 1];
    }
    return '\0';
}

void
sv_ldiscard_char(struct string_view* str)
{
    if (str && str->data && str->length > 0) {
        str->data++;
        str->length--;
    }
}

void
sv_rdiscard_char(struct string_view* str)
{
    if (str && str->data && str->length > 0) {
        str->length--;
    }
}

void
sv_ldiscard(struct string_view* str, size_t n)
{
    if (str == NULL || str->data == NULL || str->length == 0 || n == 0) {
        return;
    }

    if (n >= str->length) {
        str->data += str->length;
        str->length = 0;
        return;
    }

    str->data += n;
    str->length -= n;
}

void
sv_rdiscard(struct string_view* str, size_t n)
{
    if (str == NULL || str->data == NULL || str->length == 0 || n == 0) {
        return;
    }

    if (n >= str->length) {
        str->length = 0;
    }
    else {
        str->length -= n;
    }
}

void
sv_lstrip(struct string_view* str)
{
    if (!str || !str->length || !str->data) {
        return;
    }
    while (isspace(sv_lpeek(*str))) {
        sv_ldiscard_char(str);
    }
}

void
sv_rstrip(struct string_view* str)
{
    if (!str || !str->length || !str->data) {
        return;
    }
    while (isspace(sv_rpeek(*str))) {
        sv_rdiscard_char(str);
    }
}

void
sv_strip(struct string_view* str)
{
    sv_lstrip(str);
    sv_rstrip(str);
}

char
sv_char_at(struct string_view str, int index)
{
    if (index < 0 && index >= -((int)str.length)) {
        return str.data[(int)str.length + index];
    }
    if ((size_t)index < str.length) {
        return str.data[index];
    }
    return '\0';
}

bool
sv_starts_with(struct string_view str, struct string_view other)
{
    if (!str.length || !other.length || other.length > str.length) {
        return false;
    }
    for (size_t i = 0; i < other.length; i++) {
        if (other.data[i] != str.data[i]) {
            return false;
        }
    }
    return true;
}

bool
sv_starts_with_cstr(struct string_view str, const char* other)
{
    return sv_starts_with(str, SV_CSTR(other));
}

bool
sv_ends_with(struct string_view str, struct string_view other)
{
    if (!str.length || !other.length || other.length > str.length) {
        return false;
    }
    for (size_t i = 0; i < other.length; i++) {
        if (other.data[other.length - 1 - i] != str.data[str.length - 1 - i]) {
            return false;
        }
    }
    return true;
}

bool
sv_ends_with_cstr(struct string_view str, const char* other)
{
    return sv_ends_with(str, SV_CSTR(other));
}

bool
sv_contains(struct string_view str, struct string_view other)
{
    if (!str.length || !other.length || other.length > str.length) {
        return false;
    }
    size_t stop = str.length - other.length;
    for (size_t i = 0; i <= stop; i++) {
        if (sv_starts_with(str, other)) {
            return true;
        }
        sv_ldiscard_char(&str);
    }
    return false;
}

bool
sv_contains_cstr(struct string_view str, const char* other)
{
    return sv_contains(str, SV_CSTR(other));
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
