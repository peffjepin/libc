#include "cli.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CLI_ERROR_IS_SET(error) ((error) != NULL && (error)->code != CLI_CODE_SUCCESS)

#define CLI_WRITE_ERRORF(error, fmt, ...)                                                          \
    do {                                                                                           \
        if (!(error)) {                                                                            \
            fprintf(stderr, fmt, __VA_ARGS__);                                                     \
            fprintf(stderr, "\n");                                                                 \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
        if (snprintf((error)->reason, sizeof((error)->reason), fmt, __VA_ARGS__) >=                \
            (int)sizeof((error)->reason)) {                                                        \
            memcpy((error)->reason + sizeof((error)->reason) - 3, "..", 3);                        \
        }                                                                                          \
        (error)->code = CLI_CODE_FAILURE;                                                          \
    } while (0)

#define CLI_WRITE_ERROR(error, message)                                                            \
    do {                                                                                           \
        CLI_WRITE_ERRORF(error, "%s", message);                                                    \
    } while (0)

static const char*
cli_type_to_cstr(enum cli_type type)
{
    switch (type) {
        case CLI_STR:
            return "string";
        case CLI_INT:
            return "integer";
        case CLI_FLOAT:
            return "floating point";
        case CLI_FLAG:
            return "flag";
    }
    CLI_ASSERT(0 && "unreachable");
}

static void
write_usage_to_error(
    const char*        program_name,
    const char*        program_description,
    struct cli_param** params,
    size_t             params_count,
    struct cli_error*  error
)
{
    char usage_buffer[sizeof error->reason];
    int  written       = 0;
    int  options_count = 0;

#define uprintf(fmt, ...)                                                                          \
    written +=                                                                                     \
        snprintf(usage_buffer + written, (int)sizeof usage_buffer - written, fmt, __VA_ARGS__);    \
    if (written >= (int)sizeof usage_buffer) {                                                     \
        goto done;                                                                                 \
    }

    uprintf("%s: %s\n\n", program_name, program_description);
    uprintf("%s:\n", "positional arguments");

    for (size_t i = 0; i < params_count; i++) {
        if (params[i]->name[0] != '-') {
            uprintf(
                "\t%s - (%s) %s\n",
                params[i]->name,
                cli_type_to_cstr(params[i]->type),
                (params[i]->description) ? params[i]->description : "no description"
            );
        }
        else {
            options_count += 1;
        }
    }
    uprintf("%s", "\n");

    if (options_count) {
        uprintf("%s:\n", "options");
        for (size_t i = 0; i < params_count; i++) {
            if (params[i]->name[0] == '-') {
                uprintf(
                    "\t%s - (%s) %s\n",
                    params[i]->name,
                    cli_type_to_cstr(params[i]->type),
                    (params[i]->description) ? params[i]->description : "no description"
                );
            }
        }
    }

#undef uprintf

done:
    CLI_WRITE_ERROR(error, usage_buffer);
}

static union cli_value
cli_parse_value(const char* input, enum cli_type type, int* error)
{
    CLI_ASSERT(error);

    *error                = 0;
    union cli_value value = {0};

    if (!input || *input == '\0') {
        *error = 1;
        return value;
    }

    switch (type) {
        case CLI_FLAG:
            CLI_ASSERT(0 && "unreachable");
            break;
        case CLI_STR:
            value.str = input;
            break;
        case CLI_INT: {
            char* endptr;
            value.i64 = strtoll(input, &endptr, 10);
            if (*endptr != '\0') {
                *error = 1;
            }
            break;
        }
        case CLI_FLOAT: {
            char* endptr;
            value.f64 = strtod(input, &endptr);
            if (*endptr != '\0') {
                *error = 1;
            }
            break;
        }
    }

    return value;
}

#ifndef ARGC_MAX
#define ARGC_MAX 256
#endif

void
cli_parse_args(
    const char*        program_description,
    size_t             params_count,
    struct cli_param** params,
    int                argc,
    const char**       argv,
    struct cli_error*  error
)
{
    CLI_ASSERT(argc > 0);
    CLI_ASSERT(params_count > 0);

    if (argc > ARGC_MAX) {
        CLI_WRITE_ERRORF(
            error, "max argc of %i exceeded -- `#define ARGC_MAX (n)` to raise capacity", ARGC_MAX
        );
        return;
    }
    for (size_t i = 1; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            write_usage_to_error(argv[0], program_description, params, params_count, error);
        }
        else if (strcmp(argv[i], "-help") == 0) {
            write_usage_to_error(argv[0], program_description, params, params_count, error);
        }
    }
    if (CLI_ERROR_IS_SET(error)) {
        return;
    }

    bool already_parsed[ARGC_MAX] = {false};

    // parse options
    //
    for (size_t param = 0; param < params_count; param++) {
        if (params[param]->name[0] != '-') {
            continue;
        }

        bool param_successfully_parsed = false;

        for (int arg = 1; !param_successfully_parsed && arg < argc; arg++) {
            if (already_parsed[arg]) {
                continue;
            }
            if (strcmp(argv[arg], params[param]->name) == 0) {
                already_parsed[arg] = true;

                if (params[param]->type == CLI_FLAG) {
                    params[param]->value.present = true;
                }
                else {
                    if (++arg >= argc) {
                        CLI_WRITE_ERRORF(
                            error, "option %s has no value specified", params[param]->name
                        );
                        return;
                    }
                    int parsing_error = 0;
                    params[param]->value =
                        cli_parse_value(argv[arg], params[param]->type, &parsing_error);
                    if (parsing_error) {
                        CLI_WRITE_ERRORF(
                            error,
                            "option `%s` (%s) given value (%s) is invalid",
                            params[param]->name,
                            cli_type_to_cstr(params[param]->type),
                            argv[arg]
                        );
                        return;
                    }
                    already_parsed[arg] = true;
                }

                param_successfully_parsed = true;
            }
        }
        if (!param_successfully_parsed && params[param]->flags & CLI_FLAG_OPTION_REQUIRED) {
            CLI_WRITE_ERRORF(error, "required option %s is missing", params[param]->name);
            return;
        }
    }

    // parse positional args
    //
    for (size_t param = 0; param < params_count; param++) {
        if (params[param]->name[0] == '-') {
            continue;
        }
        bool param_successfully_parsed = false;
        for (int arg = 1; !param_successfully_parsed && arg < argc; arg++) {
            if (already_parsed[arg]) {
                continue;
            }
            int parsing_error    = 0;
            params[param]->value = cli_parse_value(argv[arg], params[param]->type, &parsing_error);
            if (parsing_error) {
                CLI_WRITE_ERRORF(
                    error,
                    "positional argument `%s` (%s) given value (%s) is invalid",
                    params[param]->name,
                    cli_type_to_cstr(params[param]->type),
                    argv[arg]
                );
                return;
            }
            already_parsed[arg]       = true;
            param_successfully_parsed = true;
            break;
        }
        if (!param_successfully_parsed) {
            CLI_WRITE_ERRORF(error, "missing positional argument %s", params[param]->name);
            return;
        }
    }

    // TODO: warn on unrecognized args
}
