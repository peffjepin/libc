#include "filesystem.h"

static bool        platform_is_absolute_filepath(const char* filepath);
static void        platform_cwd_to_buffer(char* buffer, size_t buffer_size, struct fs_error*);
static void        platform_create_directory(const char* filepath, struct fs_error*);
static void        platform_remove_directory(const char* filepath, struct fs_error*);
static bool        platform_path_is_directory(const char* filepath);
static bool        platform_path_is_file(const char* filepath);
static bool        platform_path_exists(const char* filepath);
static void        platform_iterdir_init(struct iterdir*, struct fs_error*);
static const char* platform_iterdir_step(struct iterdir*, struct fs_error*);
static void        platform_iterdir_teardown(struct iterdir*);

#ifdef _WIN32

#define PLATFORM_PATHSEP '\\'
#define PLATFORM_PATHSEP_CSTR "\\"
#define PLATFORM_ROOT_PATH_LENGTH 3
#include <windows.h>

#else

#define PLATFORM_PATHSEP '/'
#define PLATFORM_PATHSEP_CSTR "/"
#define PLATFORM_ROOT_PATH_LENGTH 1
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#endif

struct iterdir {
    FilesystemAllocator* allocator;
    bool                 exhausted;
    struct fs_path       directory_path;
#ifdef _WIN32
    struct fs_path   search_path;
    bool             yielded_first_result;
    HANDLE           find_handle;
    WIN32_FIND_DATAA find_data;
#else
    DIR*           dir_handle;
    struct dirent* entry;
#endif
};

#include <errno.h>
#include <string.h>

static void*
fs_malloc(size_t size, FilesystemAllocator* allocator)
{
    (void)allocator;
    return FS_DEFAULT_MALLOC(size);
}

static void
fs_free(void* ptr, FilesystemAllocator* allocator)
{
    if (!ptr) {
        return;
    }
    (void)allocator;
    FS_DEFAULT_FREE(ptr);
}

#define FS_FATAL_ERRORF(fmt, ...)                                                                  \
    do {                                                                                           \
        fprintf(stderr, "[FILESYSTEM FATAL ERROR]: ");                                             \
        fprintf(stderr, fmt, __VA_ARGS__);                                                         \
        fprintf(stderr, "\n");                                                                     \
        exit(EXIT_FAILURE);                                                                        \
    } while (0)

#define FS_SET_ERRORF(error, error_code, fmt, ...)                                                 \
    do {                                                                                           \
        if (!(error)) {                                                                            \
            FS_FATAL_ERRORF(fmt, __VA_ARGS__);                                                     \
        }                                                                                          \
        (error)->code = (error_code);                                                              \
        if (snprintf((error)->reason, sizeof(error)->reason, fmt, __VA_ARGS__) >=                  \
            (int)sizeof(error)->reason) {                                                          \
            memcpy((error)->reason + sizeof(error)->reason - 3, "..", 3);                          \
        }                                                                                          \
    } while (0)

#define FS_SET_ERROR(error, error_code, message)                                                   \
    do {                                                                                           \
        FS_SET_ERRORF((error), (error_code), "%s", (message));                                     \
    } while (0)

#define FS_ERROR_IS_SET(error) (error != NULL && error->code != FS_CODE_SUCCESS)

static void
map_errno(
    const char*        context,
    int                errno_code,
    enum fs_error_code fallback,
    const char*        filepath,
    struct fs_error*   error
)
{
    const char*        reason = "";
    enum fs_error_code code   = fallback;

    switch (errno_code) {
        case EACCES:
            code   = FS_CODE_PERMISSION_DENIED;
            reason = "permission denied";
            break;
        case EBADF:
            code   = FS_CODE_INVALID_HANDLE;
            reason = strerror(errno_code);
            break;
        case EMFILE:
            code   = FS_CODE_TOO_MANY_OPEN_FILES;
            reason = "process has too many open files";
            break;
        case ENFILE:
            code   = FS_CODE_TOO_MANY_OPEN_FILES;
            reason = "system has too many open files";
            break;
        case ENOENT:
            code   = FS_CODE_FILE_NOT_FOUND;
            reason = "file does not exist";
            break;
        case ENOMEM:
            code   = FS_CODE_OUT_OF_MEMORY;
            reason = "out of memory";
            break;
        case ENOTDIR:
            code   = FS_CODE_NOT_DIRECTORY;
            reason = "file is not a directory";
            break;
        case ENOTEMPTY:
            code   = FS_CODE_DIRECTORY_NOT_EMPTY;
            reason = "directory not empty";
            break;
        case EISDIR:
            code   = FS_CODE_IS_A_DIRECTORY;
            reason = "file is a directory";
            break;
        case EEXIST:
            code   = FS_CODE_ALREADY_EXISTS;
            reason = "file already exists";
            break;
        default:
            reason = strerror(errno_code);
            break;
    }

    FS_SET_ERRORF(error, code, "%s (filepath: %s) (reason: %s)", context, filepath, reason);
}

#ifdef _WIN32
static void
map_windows_error(
    const char*        context,
    DWORD              windows_code,
    enum fs_error_code fallback,
    const char*        filepath,
    struct fs_error*   error
)
{
    char               reason_buffer[128] = {0};
    const char*        reason             = reason_buffer;
    enum fs_error_code code               = fallback;

    switch (windows_code) {
        case ERROR_ACCESS_DENIED:
            code   = FS_CODE_PERMISSION_DENIED;
            reason = "permission denied";
            break;
        case ERROR_INVALID_HANDLE:
            code   = FS_CODE_INVALID_HANDLE;
            reason = "invalid handle";
            break;
        case ERROR_TOO_MANY_OPEN_FILES:
            code   = FS_CODE_TOO_MANY_OPEN_FILES;
            reason = "system has too many open files";
            break;
        case ERROR_FILE_NOT_FOUND:
            code   = FS_CODE_FILE_NOT_FOUND;
            reason = "file does not exist";
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            code   = FS_CODE_OUT_OF_MEMORY;
            reason = "out of memory";
            break;
        case ERROR_PATH_NOT_FOUND:
            code   = FS_CODE_FILE_NOT_FOUND;
            reason = "file does not exist";
            break;
        case ERROR_ALREADY_EXISTS:
            code   = FS_CODE_ALREADY_EXISTS;
            reason = "file already exists";
            break;
        case ERROR_DIR_NOT_EMPTY:
            code   = FS_CODE_DIRECTORY_NOT_EMPTY;
            reason = "directory not empty";
            break;
        default:
            FormatMessage(
                FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,
                windows_code,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reason_buffer,
                sizeof reason_buffer,
                NULL
            );
            break;
    }

    FS_SET_ERRORF(error, code, "%s (filepath: %s) (reason: %s)", context, filepath, reason);
}
#endif

static struct fs_content
read_fs_content_internal(
    FILE* fp, const char* filepath, FilesystemAllocator* allocator, struct fs_error* error
)
{
    struct fs_content content = {0};

    // determine length of the file
    int seek_result = 0;
    seek_result     = fseek(fp, 0, SEEK_END);
    long length     = ftell(fp);
    seek_result     = seek_result || fseek(fp, 0, SEEK_SET);
    if (seek_result != 0 || length < 0) {
        FS_SET_ERRORF(
            error, FS_CODE_SEEK_FAILED, "failed to determine the size of file: %s", filepath
        );
        goto cleanup;
    }
    content.size = length;

    // read the entire file into an allocation
    content.data = fs_malloc(content.size + 1, allocator);
    if (!content.data) {
        FS_SET_ERRORF(
            error,
            FS_CODE_OUT_OF_MEMORY,
            "failed to allocate %zu bytes for file content: %s",
            content.size,
            filepath
        );
        goto cleanup;
    }
    ((char*)content.data)[content.size] = '\0';
    if (content.size != 0 && fread(content.data, content.size, 1, fp) != 1) {
        FS_SET_ERRORF(
            error,
            FS_CODE_READ_FAILED,
            "failed to read %zu bytes from file: %s",
            content.size,
            filepath
        );
        goto cleanup;
    }
cleanup:
    if (FS_ERROR_IS_SET(error)) {
        fs_free(content.data, allocator);
        content = (struct fs_content){0};
    }
    return content;
}

static bool
is_absolute_filepath(const char* filepath)
{
    FS_ASSERT(filepath);
    return platform_is_absolute_filepath(filepath);
}

static void
assert_fs_path_is_valid(const struct fs_path* path)
{
    FS_ASSERT(path);
    FS_ASSERT(path->length >= PLATFORM_ROOT_PATH_LENGTH);
    FS_ASSERT(is_absolute_filepath(path->buffer));
}

struct fs_path
fs_path_cwd(struct fs_error* error)
{
    struct fs_path intermediate;
    platform_cwd_to_buffer(intermediate.buffer, sizeof intermediate.buffer, error);
    if (FS_ERROR_IS_SET(error)) {
        return (struct fs_path){0};
    }
    return fs_path_resolve(intermediate.buffer, error);
}

static const char*
skip_chars(const char* filepath, char c)
{
    FS_ASSERT(c != '\0');
    while (*filepath == c) {
        filepath++;
    }
    return filepath;
}

static void
fs_path_append_char(struct fs_path* path, char c, struct fs_error* error)
{
    FS_ASSERT(path);

    if (path->length >= sizeof path->buffer - 1) {
        FS_SET_ERROR(error, FS_CODE_PATH_TOO_LONG, "fs_path buffer overflow");
        path->buffer[sizeof path->buffer - 1] = '\0';
        return;
    }

    path->buffer[path->length] = c;
    if (c != '\0') {
        path->length += 1;
    }
}

struct fs_path
fs_path_resolve(const char* filepath, struct fs_error* error)
{
    FS_ASSERT(filepath);

    char           expected_pathsep;
    struct fs_path result;
    result.length = 0;

    if (is_absolute_filepath(filepath)) {
        expected_pathsep = PLATFORM_PATHSEP;
        for (size_t i = 0; i < PLATFORM_ROOT_PATH_LENGTH; i++) {
            fs_path_append_char(&result, *filepath++, error);
            if (FS_ERROR_IS_SET(error)) {
                fs_path_append_char(&result, '\0', error);
                return (struct fs_path){0};
            }
        }
    }
    else {
        // relative filepath
        //
        expected_pathsep = '/';
        result           = fs_path_cwd(error);
        if (FS_ERROR_IS_SET(error)) {
            fs_path_append_char(&result, '\0', error);
            return (struct fs_path){0};
        }
    }


    while (1) {
        filepath            = skip_chars(filepath, expected_pathsep);
        const char* nextsep = strchr(filepath, expected_pathsep);
        if (nextsep == NULL) {
            nextsep = strchr(filepath, '\0');
        }
        const size_t segment_length = nextsep - filepath;
        if (segment_length == 0) {
            // this can only happen if the next char is '\0' since we skip sequential pathsep chars
            break;
        }

        if (strncmp(filepath, ".", segment_length) == 0) {
            ;
        }
        else if (strncmp(filepath, "..", segment_length) == 0) {
            fs_path_parent_in_place(&result);
            if (FS_ERROR_IS_SET(error)) {
                fs_path_append_char(&result, '\0', error);
                return (struct fs_path){0};
            }
        }
        else {
            if (result.buffer[result.length - 1] != PLATFORM_PATHSEP) {
                fs_path_append_char(&result, PLATFORM_PATHSEP, error);
            }
            for (size_t i = 0; i < segment_length; i++) {
                fs_path_append_char(&result, *filepath++, error);
            }
            if (FS_ERROR_IS_SET(error)) {
                fs_path_append_char(&result, '\0', error);
                return (struct fs_path){0};
            }
        }

        if (*nextsep == '\0') {
            break;
        }
        filepath = nextsep + 1;
    }

    fs_path_append_char(&result, '\0', error);
    if (FS_ERROR_IS_SET(error)) {
        return (struct fs_path){0};
    }

    return result;
}

void
fs_path_join_in_place(struct fs_path* path, const char* other, struct fs_error* error)
{
    assert_fs_path_is_valid(path);

    if (!other || *other == '\0') {
        return;
    }

    if (is_absolute_filepath(other)) {
        FS_SET_ERROR(
            error, FS_CODE_INVALID_PATH, "cannot join an absolute path into an existing path"
        );
        return;
    }

    while (1) {
        other               = skip_chars(other, '/');
        const char* nextsep = strchr(other, '/');
        if (nextsep == NULL) {
            nextsep = strchr(other, '\0');
        }
        const size_t segment_length = nextsep - other;
        if (segment_length == 0) {
            break;
        }

        if (strncmp(other, ".", segment_length) == 0) {
            other += 1;
        }
        else if (strncmp(other, "..", segment_length) == 0) {
            other += 2;
            fs_path_parent_in_place(path);
            if (FS_ERROR_IS_SET(error)) {
                return;
            }
        }
        else {
            if (path->buffer[path->length - 1] != PLATFORM_PATHSEP) {
                fs_path_append_char(path, PLATFORM_PATHSEP, error);
            }
            for (size_t i = 0; i < segment_length; i++) {
                fs_path_append_char(path, *other++, error);
            }
            if (FS_ERROR_IS_SET(error)) {
                return;
            }
        }
    }

    fs_path_append_char(path, '\0', error);
}

struct fs_path
fs_path_join(const struct fs_path* path, const char* other, struct fs_error* error)
{
    assert_fs_path_is_valid(path);
    struct fs_path copy = *path;
    fs_path_join_in_place(&copy, other, error);
    return copy;
}

void
fs_path_parent_in_place(struct fs_path* path)
{
    assert_fs_path_is_valid(path);
    if (path->length <= PLATFORM_ROOT_PATH_LENGTH) {
        return;
    }
    // remove trailing pathsep
    if (path->buffer[path->length - 1] == PLATFORM_PATHSEP) {
        path->length -= 1;
    }
    // remove path segment
    while (path->buffer[path->length - 1] != PLATFORM_PATHSEP) {
        path->length -= 1;
    }
    // remove trailing pathsep (unless it belongs to root)
    if (path->length > PLATFORM_ROOT_PATH_LENGTH) {
        path->length -= 1;
    }
    path->buffer[path->length] = '\0';
}

struct fs_path
fs_path_parent(const struct fs_path* path)
{
    assert_fs_path_is_valid(path);
    struct fs_path copy = *path;
    fs_path_parent_in_place(&copy);
    return copy;
}

void
fs_path_mkdir(const struct fs_path* path, bool force, struct fs_error* error)
{
    assert_fs_path_is_valid(path);

    if (force) {
        struct fs_path parent = fs_path_parent(path);
        if (!fs_path_exists(&parent)) {
            fs_path_mkdir(&parent, force, error);
        }
    }
    if (FS_ERROR_IS_SET(error)) {
        return;
    }

    platform_create_directory(path->buffer, error);
}

void
fs_path_rmfile(const struct fs_path* path, struct fs_error* error)
{
    assert_fs_path_is_valid(path);

    if (!fs_path_exists(path)) {
        map_errno("failed to remove file", ENOENT, FS_CODE_FILE_NOT_FOUND, path->buffer, error);
        return;
    }
    if (fs_path_is_dir(path)) {
        map_errno("failed to remove file", EISDIR, FS_CODE_IS_A_DIRECTORY, path->buffer, error);
        return;
    }

    if (remove(path->buffer) == -1) {
        map_errno("failed to remove file", errno, FS_CODE_UNSPECIFIED, path->buffer, error);
    }
}

void
fs_path_rmdir(const struct fs_path* path, bool force, struct fs_error* error)
{
    assert_fs_path_is_valid(path);

    if (!fs_path_exists(path)) {
        map_errno(
            "failed to remove directory", ENOENT, FS_CODE_FILE_NOT_FOUND, path->buffer, error
        );
        return;
    }
    if (!fs_path_is_dir(path)) {
        map_errno(
            "failed to remove directory", ENOTDIR, FS_CODE_NOT_DIRECTORY, path->buffer, error
        );
        return;
    }


    if (force) {
        FilesystemDirectoryIterator iterator = fs_iterdir(path, NULL, error);
        if (FS_ERROR_IS_SET(error)) {
            return;
        }

        struct fs_path entry;
        while (fs_iterdir_next(iterator, &entry, error)) {
            if (FS_ERROR_IS_SET(error)) {
                fs_iterdir_free(iterator);
                return;
            }

            if (fs_path_is_dir(&entry)) {
                fs_path_rmdir(&entry, force, error);
            }
            else {
                fs_path_rmfile(&entry, error);
            }
            if (FS_ERROR_IS_SET(error)) {
                fs_iterdir_free(iterator);
                return;
            }
        }

        fs_iterdir_free(iterator);
    }


    platform_remove_directory(path->buffer, error);
}

bool
fs_path_is_dir(const struct fs_path* path)
{
    assert_fs_path_is_valid(path);
    return platform_path_is_directory(path->buffer);
}

bool
fs_path_exists(const struct fs_path* path)
{
    assert_fs_path_is_valid(path);
    return platform_path_exists(path->buffer);
}

bool
fs_path_is_root(const struct fs_path* path)
{
    assert_fs_path_is_valid(path);

    return path->length == PLATFORM_ROOT_PATH_LENGTH;
}

bool
fs_path_is_file(const struct fs_path* path)
{
    assert_fs_path_is_valid(path);
    return platform_path_is_file(path->buffer);
}

const char*
fs_path_filename(const struct fs_path* path, size_t* length)
{
    assert_fs_path_is_valid(path);
    FS_ASSERT(length);

    const char* filepath = path->buffer;
    const char* start    = filepath;
    const char* stop     = NULL;

    char c;
    while (1) {
        c = *filepath;
        if (c == '.') {
            stop = filepath;
        }
        if (c == PLATFORM_PATHSEP) {
            start = filepath + 1;
        }
        if (c == '\0') {
            stop = (stop) ? stop : filepath;
            break;
        }
        filepath++;
    }

    const int difference = stop - start;
    if (difference <= 0) {
        *length = 0;
        return filepath;
    }

    *length = (size_t)difference;
    return start;
}

const char*
fs_path_ext(const struct fs_path* path, size_t* length)
{
    assert_fs_path_is_valid(path);
    FS_ASSERT(length);

    size_t      filename_length;
    const char* filename = fs_path_filename(path, &filename_length);
    const char* last_dot = strrchr(filename, '.');
    if (!last_dot) {
        *length = 0;
        return "";
    }
    *length = strlen(last_dot + 1);
    return last_dot + 1;
}

struct fs_content
fs_path_read_text(
    const struct fs_path* path, FilesystemAllocator* allocator, struct fs_error* error
)
{
    assert_fs_path_is_valid(path);
    if (fs_path_is_dir(path)) {
        map_errno("failed to read file", EISDIR, FS_CODE_IS_A_DIRECTORY, path->buffer, error);
        return (struct fs_content){0};
    }
    return fs_read_file_text(path->buffer, allocator, error);
}

struct fs_content
fs_path_read_binary(
    const struct fs_path* path, FilesystemAllocator* allocator, struct fs_error* error
)
{
    assert_fs_path_is_valid(path);
    if (fs_path_is_dir(path)) {
        map_errno("failed to read file", EISDIR, FS_CODE_IS_A_DIRECTORY, path->buffer, error);
        return (struct fs_content){0};
    }
    return fs_read_file_binary(path->buffer, allocator, error);
}

void
fs_path_write(const struct fs_path* path, void* data, size_t nbytes, struct fs_error* error)
{
    assert_fs_path_is_valid(path);
    fs_write_file(path->buffer, data, nbytes, error);
}

FilesystemDirectoryIterator
fs_iterdir(const struct fs_path* path, FilesystemAllocator* allocator, struct fs_error* error)
{
    assert_fs_path_is_valid(path);

    if (!fs_path_exists(path)) {
        map_errno(
            "failed to iterate directory", ENOENT, FS_CODE_FILE_NOT_FOUND, path->buffer, error
        );
        return NULL;
    }
    if (!fs_path_is_dir(path)) {
        map_errno(
            "failed to iterate directory", ENOTDIR, FS_CODE_NOT_DIRECTORY, path->buffer, error
        );
        return NULL;
    }

    struct iterdir* iterator;
    iterator                 = fs_malloc(sizeof *iterator, allocator);
    iterator->allocator      = allocator;
    iterator->directory_path = *path;
    iterator->exhausted      = false;
    platform_iterdir_init(iterator, error);
    if (FS_ERROR_IS_SET(error)) {
        fs_free(iterator, allocator);
        return NULL;
    }
    return iterator;
}

static const char*
fs_iterdir_step(struct iterdir* iterator, struct fs_error* error)
{
    FS_ASSERT(iterator);
    if (iterator->exhausted) {
        return NULL;
    }
    return platform_iterdir_step(iterator, error);
}

bool
fs_iterdir_next(struct iterdir* iterator, struct fs_path* outpath, struct fs_error* error)
{
    FS_ASSERT(iterator);
    FS_ASSERT(outpath);

    const char* filename = NULL;

    do {
        filename = fs_iterdir_step(iterator, error);
        if (!filename || FS_ERROR_IS_SET(error)) {
            return false;
        }
    } while (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0);

    *outpath = fs_path_join(&iterator->directory_path, filename, error);
    return !FS_ERROR_IS_SET(error);
}

void
fs_iterdir_free(struct iterdir* iterator)
{
    if (!iterator) {
        return;
    }
    platform_iterdir_teardown(iterator);
    fs_free(iterator, iterator->allocator);
}

struct fs_content
fs_read_file_binary(const char* filepath, FilesystemAllocator* allocator, struct fs_error* error)
{
    FILE* file = fs_open(filepath, "rb", error);
    if (FS_ERROR_IS_SET(error)) return (struct fs_content){0};

    struct fs_content content = read_fs_content_internal(file, filepath, allocator, error);
    fs_close(file);

    return content;
}

struct fs_content
fs_read_file_text(const char* filepath, FilesystemAllocator* allocator, struct fs_error* error)
{
    FILE* file = fs_open(filepath, "r", error);
    if (FS_ERROR_IS_SET(error)) return (struct fs_content){0};

    struct fs_content content = read_fs_content_internal(file, filepath, allocator, error);
    fs_close(file);

    return content;
}

void
fs_write_file(const char* filepath, const void* data, size_t data_size, struct fs_error* error)
{
    FILE* file = fs_open(filepath, "wb", error);
    if (FS_ERROR_IS_SET(error)) return;

    // write the file data to the file
    if (fwrite(data, data_size, 1, file) != 1) {
        FS_SET_ERRORF(
            error,
            FS_CODE_WRITE_FAILED,
            "failed to write %zu bytes to file: %s",
            data_size,
            filepath
        );
        fs_close(file);
        return;
    }

    fs_close(file);
}

FILE*
fs_open(const char* filepath, const char* mode, struct fs_error* error)
{
    FILE* file = fopen(filepath, mode);
    if (file == NULL) {
        map_errno("failed to open file", errno, FS_CODE_OPEN_FAILED, filepath, error);
        return NULL;
    }
    return file;
}

void
fs_close(FILE* fp)
{
    if (fp) fclose(fp);
}

static bool
platform_is_absolute_filepath(const char* filepath)
{
#ifdef _WIN32
    const char c = *filepath++;
    if (c < 'A' || c > 'Z' || *filepath++ != ':' || *filepath++ != '\\') {
        return false;
    }
    return true;
#else
    return *filepath == '/';
#endif
}

static void
platform_cwd_to_buffer(char* buffer, size_t buffer_size, struct fs_error* error)
{
#ifdef _WIN32
    if (GetCurrentDirectory(buffer_size, buffer) == 0) {
        map_windows_error("failed to get cwd", GetLastError(), FS_CODE_PATH_TOO_LONG, "", error);
    }
#else
    if (getcwd(buffer, buffer_size) == NULL) {
        map_errno("failed to get cwd", errno, FS_CODE_PATH_TOO_LONG, "", error);
    }
#endif
}

static void
platform_create_directory(const char* filepath, struct fs_error* error)
{
#ifdef _WIN32
    if (CreateDirectory(filepath, NULL) == 0) {
        map_windows_error(
            "failed to create directory", GetLastError(), FS_CODE_OPEN_FAILED, filepath, error
        );
    }
#else
    if (mkdir(filepath, 0777) != 0) {
        map_errno("failed to create directory", errno, FS_CODE_OPEN_FAILED, filepath, error);
    }
#endif
}

static void
platform_remove_directory(const char* filepath, struct fs_error* error)
{
#ifdef _WIN32
    if (!RemoveDirectory(filepath)) {
        map_windows_error(
            "failed to remove directory", GetLastError(), FS_CODE_UNSPECIFIED, filepath, error
        );
    }
#else
    if (remove(filepath) == -1) {
        map_errno("failed to remove directory", errno, FS_CODE_UNSPECIFIED, filepath, error);
    }
#endif
}

static bool
platform_path_is_directory(const char* filepath)
{
#ifdef _WIN32
    DWORD fileAttributes = GetFileAttributes(filepath);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (bool)(fileAttributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

static bool
platform_path_is_file(const char* filepath)
{
#ifdef _WIN32
    DWORD fileAttributes = GetFileAttributes(filepath);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return !(bool)(fileAttributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

static bool
platform_path_exists(const char* filepath)
{
#ifdef _WIN32
    return (GetFileAttributes(filepath) != INVALID_FILE_ATTRIBUTES);
#else
    return access(filepath, F_OK) == 0;
#endif
}

static void
platform_iterdir_init(struct iterdir* iterator, struct fs_error* error)
{
#ifdef _WIN32
    // Add a wildcard at the end of the directory path
    //
    iterator->search_path = iterator->directory_path;
    fs_path_append_char(&iterator->search_path, PLATFORM_PATHSEP, error);
    fs_path_append_char(&iterator->search_path, '*', error);
    fs_path_append_char(&iterator->search_path, '\0', error);
    if (FS_ERROR_IS_SET(error)) {
        return;
    }
    // Find the first file in the directory
    //
    iterator->find_handle = FindFirstFileA(iterator->search_path.buffer, &iterator->find_data);
    if (!iterator->find_handle) {
        map_windows_error(
            "failed to open directory",
            GetLastError(),
            FS_CODE_OPEN_FAILED,
            iterator->directory_path.buffer,
            error
        );
        return;
    }
#else
    // Open directory
    //
    iterator->dir_handle = opendir(iterator->directory_path.buffer);
    if (iterator->dir_handle == NULL) {
        map_errno(
            "failed to open directory",
            errno,
            FS_CODE_OPEN_FAILED,
            iterator->directory_path.buffer,
            error
        );
        return;
    }
#endif
}

static const char*
platform_iterdir_step(struct iterdir* iterator, struct fs_error* error)
{
#ifdef _WIN32
    if (!iterator->yielded_first_result) {
        iterator->yielded_first_result = true;
        return iterator->find_data.cFileName;
    }
    if (!FindNextFileA(iterator->find_handle, &iterator->find_data)) {
        iterator->exhausted = true;
        DWORD windows_error = GetLastError();
        if (windows_error != ERROR_NO_MORE_FILES) {
            map_windows_error(
                "failed to iterate to next file in directory",
                windows_error,
                FS_CODE_UNSPECIFIED,
                iterator->directory_path.buffer,
                error
            );
        }
        return NULL;
    }
    return iterator->find_data.cFileName;
#else
    errno           = 0;
    iterator->entry = readdir(iterator->dir_handle);
    if (!iterator->entry) {
        if (errno) {
            map_errno(
                "failed to iterate to next file in directory",
                errno,
                FS_CODE_UNSPECIFIED,
                iterator->directory_path.buffer,
                error
            );
        }
        iterator->exhausted = true;
        return NULL;
    }
    return iterator->entry->d_name;
#endif
}

static void
platform_iterdir_teardown(struct iterdir* iterator)
{
#ifdef _WIN32
    FindClose(iterator->find_handle);
#else
    closedir(iterator->dir_handle);
#endif
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
