#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef CLI_ASSERT
#include <assert.h>
#define CLI_ASSERT assert
#endif

enum cli_type {
    CLI_STR = 0,
    CLI_INT,
    CLI_FLOAT,
    CLI_FLAG,
};

union cli_value {
    const char* str;
    int64_t     i64;
    double      f64;
    bool        present;
};

struct cli_validation {
    enum {
        CLI_VALIDATION_TYPES_ONLY = 0,
        CLI_VALIDATION_RANGE,
        CLI_VALIDATION_CHOICES,
    } strategy;

    union {
        struct {
            union cli_value start;  // inclusive
            union cli_value stop;   // exclusive
        } range;

        struct {
            size_t           count;
            union cli_value* values;
        } choices;
    };
};

enum cli_flags {
    CLI_FLAG_OPTION_REQUIRED = 1u << 0,
};

struct cli_param {
    const char*           name;
    enum cli_type         type;
    uint32_t              flags;
    struct cli_validation validation;
    const char*           description;
    union cli_value       value;
};

struct cli_error {
    enum {
        CLI_CODE_SUCCESS = 0,
        CLI_CODE_FAILURE,
    } code;

    char reason[1024];
};

void cli_parse_args(
    const char*        program_description,
    size_t             params_count,
    struct cli_param** params,
    int                argc,
    const char**       argv,
    struct cli_error*  error
);

#endif  // CLI_H
