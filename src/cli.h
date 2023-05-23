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
    CLI_FLAG,
};

union cli_value {
    int64_t     i64;
    double      f64;
    const char* str;
    bool        present;
};

struct cli_validation {
    enum {
        CLI_VALIDATION_NONE = 0,
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

struct cli_param {
    const char*           name;
    const char*           short_name;
    enum cli_type         type;
    struct cli_validation validation;
    bool                  required;
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
    struct cli_param** params,
    size_t             params_count,
    int                argc,
    const char**       argv,
    struct cli_error*  error
);

#endif  // CLI_H
