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
    int  written = 0;

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
            uprintf("\t%s - %s\n", params[i]->name, params[i]->description);
        }
    }
    uprintf("%s", "\n");

    uprintf("%s:\n", "options");
    for (size_t i = 0; i < params_count; i++) {
        if (params[i]->name[0] == '-') {
            uprintf("\t%s - %s\n", params[i]->name, params[i]->description);
        }
    }

#undef uprintf

done:
    CLI_WRITE_ERROR(error, usage_buffer);
}

#define ARGC_MAX 256

void
cli_parse_args(
    const char*        program_description,
    struct cli_param** params,
    size_t             params_count,
    int                argc,
    const char**       argv,
    struct cli_error*  error
)
{
    CLI_ASSERT(argc > 0);
    CLI_ASSERT(params_count > 0);

    if (argc > ARGC_MAX) {
        CLI_WRITE_ERRORF(error, "max argc of %i exceeded -- consider raising ARGC_MAX", ARGC_MAX);
        return;
    }
    for (size_t i = 1; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            write_usage_to_error(argv[0], program_description, params, params_count, error);
        }
        else if (strcmp(argv[i], "-help") == 0) {
            write_usage_to_error(argv[0], program_description, params, params_count, error);
        }
        else if (strcmp(argv[i], "help") == 0) {
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
                    params[param]->value.str = argv[arg];
                    already_parsed[arg]      = true;
                }

                param_successfully_parsed = true;
            }
        }
        if (!param_successfully_parsed && params[param]->required) {
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
            params[param]->value.str  = argv[arg];
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

#ifdef CLI_TEST_MAIN

#ifndef TEST_ASSERT
#include <assert.h>
#define TEST_ASSERT assert
#endif  // TEST_ASSERT


static void
assert_error_contains(struct cli_error* error, const char* expected)
{
    size_t      expected_length = strlen(expected);
    const char* error_reason    = error->reason;
    while (*error_reason != '\0') {
        if (strncmp(error_reason++, expected, expected_length) == 0) {
            return;
        }
    }
    fprintf(stderr, "expected (%s) not found in error: \n%s\n", expected, error->reason);
    TEST_ASSERT(0);
}

int
main(void)
{
    printf("cli tests passed\n");

    // test --help
    {
        struct cli_param param = {
            .name        = "param1",
            .description = "example positional param",
        };
        struct cli_error error               = {0};
        const char*      program_name        = "example";
        const char*      program_description = "example description";

        cli_parse_args(
            program_description,
            (struct cli_param*[]){&param},
            1,
            2,
            (const char*[]){program_name, "--help"},
            &error
        );

        assert_error_contains(&error, program_name);
        assert_error_contains(&error, program_description);
        assert_error_contains(&error, param.name);
        assert_error_contains(&error, param.description);
    }

    return 0;
}

#endif  // CLI_TEST_MAIN
