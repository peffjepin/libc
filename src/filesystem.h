#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef FS_ASSERT
#include <assert.h>
#define FS_ASSERT assert
#endif

#ifndef FS_DEFAULT_MALLOC
#include <stdlib.h>
#define FS_DEFAULT_MALLOC malloc
#endif

#ifndef FS_DEFAULT_FREE
#include <stdlib.h>
#define FS_DEFAULT_FREE free
#endif

#ifndef FS_PATH_MAX
#define FS_PATH_MAX 512
#endif

enum fs_error_code {
    FS_CODE_SUCCESS = 0,
    FS_CODE_OUT_OF_MEMORY,
    FS_CODE_SEEK_FAILED,
    FS_CODE_READ_FAILED,
    FS_CODE_WRITE_FAILED,
    FS_CODE_OPEN_FAILED,
    FS_CODE_PERMISSION_DENIED,
    FS_CODE_INVALID_HANDLE,
    FS_CODE_INVALID_PATH,
    FS_CODE_TOO_MANY_OPEN_FILES,
    FS_CODE_FILE_NOT_FOUND,
    FS_CODE_IS_A_DIRECTORY,
    FS_CODE_NOT_DIRECTORY,
    FS_CODE_DIRECTORY_NOT_EMPTY,
    FS_CODE_ALREADY_EXISTS,
    FS_CODE_PATH_TOO_LONG,
    FS_CODE_UNSPECIFIED,
};

struct fs_error {
    enum fs_error_code code;
    char               reason[128];
};

struct fs_path {
    char   buffer[FS_PATH_MAX];
    size_t length;
};

struct fs_content {
    size_t size;
    void*  data;
};

typedef void            FilesystemAllocator;
typedef struct iterdir* FilesystemDirectoryIterator;

// clang-format off

FILE*             fs_open(const char* filepath, const char* mode, struct fs_error*);
void              fs_close(FILE*);
struct fs_content fs_read_file_binary(const char* filepath, FilesystemAllocator*, struct fs_error*);
struct fs_content fs_read_file_text(const char* filepath, FilesystemAllocator*, struct fs_error*);
void              fs_write_file(const char* filepath, const void* data, size_t data_size, struct fs_error*);

struct fs_path    fs_path_cwd(struct fs_error*);
struct fs_path    fs_path_resolve(const char* filepath, struct fs_error*);
struct fs_path    fs_path_join(const struct fs_path*, const char* other, struct fs_error*);
struct fs_path    fs_path_parent(const struct fs_path*);
void              fs_path_join_in_place(struct fs_path*, const char* other, struct fs_error*);
void              fs_path_parent_in_place(struct fs_path*);
void              fs_path_mkdir(const struct fs_path*, bool force, struct fs_error*);
void              fs_path_rmdir(const struct fs_path*, bool force, struct fs_error*);
bool              fs_path_is_dir(const struct fs_path*);
bool              fs_path_exists(const struct fs_path*);
bool              fs_path_is_root(const struct fs_path*);
bool              fs_path_is_file(const struct fs_path*);
void              fs_path_rmfile(const struct fs_path*, struct fs_error*);
const char*       fs_path_filename(const struct fs_path*, size_t* length);
const char*       fs_path_ext(const struct fs_path*, size_t* length);
void              fs_path_write(const struct fs_path*, void* data, size_t nbytes, struct fs_error*);
struct fs_content fs_path_read_text(const struct fs_path*, FilesystemAllocator*, struct fs_error*);
struct fs_content fs_path_read_binary(const struct fs_path*, FilesystemAllocator*, struct fs_error*);

FilesystemDirectoryIterator
     fs_iterdir(const struct fs_path*, FilesystemAllocator*, struct fs_error*);
bool fs_iterdir_next(FilesystemDirectoryIterator, struct fs_path* outpath, struct fs_error*);
void fs_iterdir_free(FilesystemDirectoryIterator);

// clang-format on

#ifdef FILESYSTEM_TEST_MAIN

#include <string.h>
#include <assert.h>
#define TEST_ASSERT(expr) assert(expr)
#define ASSERT_PATHS_EQUAL(path1, path2) TEST_ASSERT(strcmp((path1).buffer, (path2).buffer) == 0)

int
main(int argc, char** argv)
{
    (void)argc;
    struct fs_path test_dir = fs_path_resolve("build/test_directory", NULL);

    // path resolution
    //
    {
        // fs_path_resolve resolves relative to cwd
        //
        // fs_path_* uses '/' as a separator by default
        //
        struct fs_path cwd1 = fs_path_cwd(NULL);
        struct fs_path cwd2 = fs_path_resolve(".", NULL);
        struct fs_path cwd3 = fs_path_resolve("./././", NULL);
        struct fs_path cwd4 = fs_path_resolve("", NULL);
        struct fs_path cwd5 = fs_path_join(&test_dir, "../..", NULL);
        ASSERT_PATHS_EQUAL(cwd2, cwd1);
        ASSERT_PATHS_EQUAL(cwd3, cwd1);
        ASSERT_PATHS_EQUAL(cwd4, cwd1);
        ASSERT_PATHS_EQUAL(cwd5, cwd1);

        // fs_path should recognize the special case where it has been passed an
        // absolute path and expects that an absolute path will contain platform specific
        // path separators
        //
        struct fs_path cwd_from_absolute = fs_path_resolve(cwd1.buffer, NULL);
        ASSERT_PATHS_EQUAL(cwd1, cwd_from_absolute);
    }

    // path overflow
    //
    {
        char long_string[FS_PATH_MAX + 1];
        for (size_t i = 0; i < sizeof long_string; i++) {
            long_string[i] = 'a';
        }
        long_string[sizeof long_string - 1] = '\0';

        struct fs_error error = {0};
        fs_path_resolve(long_string, &error);
        TEST_ASSERT(error.code == FS_CODE_PATH_TOO_LONG);
    }

    // filename
    //
    {
        struct fs_path path = fs_path_resolve("build", NULL);
        size_t         length;
        const char*    name = fs_path_filename(&path, &length);
        TEST_ASSERT(length == 5);
        TEST_ASSERT(strncmp(name, "build", length) == 0);

        path = fs_path_resolve("build1/build2", NULL);
        name = fs_path_filename(&path, &length);
        TEST_ASSERT(length == 6);
        TEST_ASSERT(strncmp(name, "build2", length) == 0);

        path = fs_path_resolve("build1/build2.ext", NULL);
        name = fs_path_filename(&path, &length);
        TEST_ASSERT(length == 6);
        TEST_ASSERT(strncmp(name, "build2", length) == 0);

        path = fs_path_resolve("build1/build2.ext.ext", NULL);
        name = fs_path_filename(&path, &length);
        TEST_ASSERT(length == 10);
        TEST_ASSERT(strncmp(name, "build2.ext", length) == 0);
    }

    // file extension
    //
    {
        struct fs_path path = fs_path_resolve("build", NULL);
        size_t         length;
        const char*    ext = fs_path_ext(&path, &length);
        TEST_ASSERT(length == 0);
        TEST_ASSERT(strncmp(ext, "", length) == 0);

        path = fs_path_resolve("test.ext", NULL);
        ext  = fs_path_ext(&path, &length);
        TEST_ASSERT(length == 3);
        TEST_ASSERT(strncmp(ext, "ext", length) == 0);

        path = fs_path_resolve("test.ext1.ext2", NULL);
        ext  = fs_path_ext(&path, &length);
        TEST_ASSERT(length == 4);
        TEST_ASSERT(strncmp(ext, "ext2", length) == 0);

        path = fs_path_resolve("dir/test.ext1.ext2", NULL);
        ext  = fs_path_ext(&path, &length);
        TEST_ASSERT(length == 4);
        TEST_ASSERT(strncmp(ext, "ext2", length) == 0);
    }

    // parent
    //
    {
        struct fs_path path1 = fs_path_resolve("build", NULL);
        struct fs_path path2 = fs_path_parent(&test_dir);
        ASSERT_PATHS_EQUAL(path1, path2);
    }

    // mkdir
    //
    {
        // setup test directory
        //
        fs_path_mkdir(&test_dir, true, NULL);

        // error if directory exists
        //
        struct fs_error error = {0};
        fs_path_mkdir(&test_dir, true, &error);
        TEST_ASSERT(error.code == FS_CODE_ALREADY_EXISTS);

        // error when force=false and parent does not exist
        //
        error                 = (struct fs_error){0};
        struct fs_path nested = fs_path_join(&test_dir, "nested1/nested2/nested3", NULL);
        fs_path_mkdir(&nested, false, &error);
        TEST_ASSERT(error.code == FS_CODE_FILE_NOT_FOUND);

        // force = true nested directory made no problem
        //
        fs_path_mkdir(&nested, true, NULL);
    }

    // exists
    //
    {
        // does exist
        //
        TEST_ASSERT(fs_path_exists(&test_dir));

        // does not exist
        //
        struct fs_path path = fs_path_join(&test_dir, "does_not_exist", NULL);
        TEST_ASSERT(!fs_path_exists(&path));
    }

    // is_dir
    //
    {
        // is a directory
        //
        TEST_ASSERT(fs_path_is_dir(&test_dir));

        // does not exist
        //
        struct fs_path path = fs_path_join(&test_dir, "does_not_exist", NULL);
        TEST_ASSERT(!fs_path_is_dir(&path));

        // is a file
        //
        path = fs_path_resolve(*argv, NULL);
        TEST_ASSERT(!fs_path_is_dir(&path));
    }

    // fs_write_file
    // fs_read_file_binary
    //
    {
        int            value = 123;
        struct fs_path path  = fs_path_join(&test_dir, "new_file", NULL);
        fs_path_write(&path, &value, sizeof value, NULL);

        // load file that does exist
        //
        struct fs_content content = fs_path_read_binary(&path, NULL, NULL);
        TEST_ASSERT(content.size == sizeof value && content.data);
        TEST_ASSERT(*(int*)content.data == 123);
        free(content.data);

        // load file that does not exist
        //
        struct fs_error error = {0};
        path                  = fs_path_join(&test_dir, "does_not_exist", NULL);
        content               = fs_path_read_binary(&path, NULL, &error);
        TEST_ASSERT(error.code == FS_CODE_FILE_NOT_FOUND);

        // load file that is a directory
        //
        error   = (struct fs_error){0};
        content = fs_path_read_binary(&test_dir, NULL, &error);
        TEST_ASSERT(error.code == FS_CODE_IS_A_DIRECTORY);
    }

    // fs_write_file
    // fs_read_file_text
    //
    {
        struct fs_path path = fs_path_join(&test_dir, "new_text_file", NULL);
        TEST_ASSERT(!fs_path_is_file(&path));
        fs_path_write(&path, "hello", 5, NULL);

        // load file that does exist
        //
        struct fs_content content = fs_path_read_text(&path, NULL, NULL);
        TEST_ASSERT(content.size == 5);
        TEST_ASSERT(content.data && strcmp(content.data, "hello") == 0);
        TEST_ASSERT(fs_path_is_file(&path));
        free(content.data);

        // load file that does not exist
        //
        struct fs_error error = {0};
        path                  = fs_path_join(&test_dir, "does_not_exist", NULL);
        content               = fs_path_read_text(&path, NULL, &error);
        TEST_ASSERT(error.code == FS_CODE_FILE_NOT_FOUND);

        // load file that is a directory
        //
        error   = (struct fs_error){0};
        content = fs_path_read_binary(&test_dir, NULL, &error);
        TEST_ASSERT(error.code == FS_CODE_IS_A_DIRECTORY);
    }

    // iterdir
    //
    {
        // the files we have created
        //
        struct fs_path paths[] = {
            fs_path_join(&test_dir, "new_file", NULL),
            fs_path_join(&test_dir, "new_text_file", NULL),
            fs_path_join(&test_dir, "nested1", NULL),
        };
        bool seen[sizeof paths / sizeof *paths] = {0};

        FilesystemDirectoryIterator iterator = fs_iterdir(&test_dir, NULL, NULL);
        struct fs_path              iterated;

        while (fs_iterdir_next(iterator, &iterated, NULL)) {
            for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
                if (strcmp(iterated.buffer, paths[i].buffer) == 0) {
                    // we should not see repeats
                    //
                    TEST_ASSERT(!seen[i]);
                    seen[i] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
            // we should have seen all of the expected files
            //
            TEST_ASSERT(seen[i]);
        }

        fs_iterdir_free(iterator);
    }

    // rmdir
    //
    {
        // fails when directory is not empty and force == false
        //
        struct fs_error error = {0};
        fs_path_rmdir(&test_dir, false, &error);
        TEST_ASSERT(error.code == FS_CODE_DIRECTORY_NOT_EMPTY);

        // fails when directory does not exist
        //
        error                         = (struct fs_error){0};
        struct fs_path does_not_exist = fs_path_join(&test_dir, "does_not_exists", NULL);
        fs_path_rmdir(&does_not_exist, false, &error);
        TEST_ASSERT(error.code == FS_CODE_FILE_NOT_FOUND);

        // fails when path points to a file
        //
        error               = (struct fs_error){0};
        struct fs_path file = fs_path_join(&test_dir, "new_file", NULL);
        fs_path_rmdir(&file, false, &error);
        TEST_ASSERT(error.code == FS_CODE_NOT_DIRECTORY);

        // succeeds when directory is empty and force == false
        //
        struct fs_path emptydir = fs_path_join(&test_dir, "nested1/nested2/nested3", NULL);
        fs_path_rmdir(&emptydir, false, NULL);
        TEST_ASSERT(!fs_path_exists(&emptydir));

        // succeeds when directory is not empty and force == true
        //
        fs_path_rmdir(&test_dir, true, NULL);
        TEST_ASSERT(!fs_path_exists(&test_dir));
    }

    printf("%s tests passed\n", __FILE__);
}

#endif  // FILESYSTEM_TEST_MAIN

#endif  // FILESYSTEM_H

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
