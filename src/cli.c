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
    fprintf(stderr, "expected value (%s) not found in error: \n%s\n", expected, error->reason);
    TEST_ASSERT(0);
}

static void
assert_f64_equal(double f1, double f2)
{
    static const double eps  = 1e-6;
    double              diff = f2 - f1;
    if (diff < 0) {
        diff = -diff;
    }
    TEST_ASSERT(diff < eps);
}

struct expected_value {
    const char*     input;
    union cli_value value;
};

int
main(void)
{
    const char* program_name        = "example";
    const char* program_description = "example description";

    // test --help
    //
    {
        struct cli_param param1 = {
            .name        = "param1",
            .description = "example positional param",
            .type        = CLI_STR,
        };
        struct cli_param param2 = {
            .name        = "-option1",
            .description = "example option",
            .type        = CLI_INT,
        };

        const char* help_options[] = {"-help", "--help"};
        for (size_t i = 0; i < sizeof help_options / sizeof *help_options; i++) {
            struct cli_error error = {0};
            cli_parse_args(
                program_description,
                2,
                (struct cli_param*[]){&param1, &param2},
                2,
                (const char*[]){program_name, help_options[i]},
                &error
            );
            TEST_ASSERT(error.code == CLI_CODE_FAILURE);
            assert_error_contains(&error, program_name);
            assert_error_contains(&error, program_description);
            assert_error_contains(&error, param1.name);
            assert_error_contains(&error, param1.description);
            assert_error_contains(&error, "string");
            assert_error_contains(&error, param2.name);
            assert_error_contains(&error, param2.description);
            assert_error_contains(&error, "int");
        }
    }

    // positional args given
    //
    {
        struct cli_param param1 = {
            .name = "first param",
        };
        struct cli_param param2 = {
            .name = "second param",
        };

        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            3,
            (const char*[]){program_name, "val1", "val2"},
            NULL
        );

        TEST_ASSERT(strcmp(param1.value.str, "val1") == 0);
        TEST_ASSERT(strcmp(param2.value.str, "val2") == 0);
    }

    // positional arg missing
    //
    {
        struct cli_param param1 = {
            .name = "first param",
        };
        struct cli_param param2 = {
            .name = "second param",
        };

        struct cli_error error = {0};
        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            2,
            (const char*[]){program_name, "val1"},
            &error
        );

        TEST_ASSERT(error.code == CLI_CODE_FAILURE);
        assert_error_contains(&error, param2.name);
        assert_error_contains(&error, "missing positional argument");
    }

    // all options present
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
        };
        struct cli_param param2 = {
            .name = "--opt2",
        };

        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            5,
            (const char*[]){program_name, "--opt1", "1", "--opt2", "2"},
            NULL
        );

        TEST_ASSERT(strcmp(param1.value.str, "1") == 0);
        TEST_ASSERT(strcmp(param2.value.str, "2") == 0);
    }

    // all options present out of order
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
        };
        struct cli_param param2 = {
            .name = "--opt2",
        };

        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            5,
            (const char*[]){program_name, "--opt2", "1", "--opt1", "2"},
            NULL
        );

        TEST_ASSERT(strcmp(param1.value.str, "2") == 0);
        TEST_ASSERT(strcmp(param2.value.str, "1") == 0);
    }

    // single option present
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
        };
        struct cli_param param2 = {
            .name = "--opt2",
        };

        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            3,
            (const char*[]){program_name, "--opt1", "1"},
            NULL
        );

        TEST_ASSERT(strcmp(param1.value.str, "1") == 0);
        TEST_ASSERT(!param2.value.str);
    }

    // required option missing
    //
    {
        struct cli_param param1 = {
            .name  = "--opt1",
            .flags = CLI_FLAG_OPTION_REQUIRED,
        };
        struct cli_param param2 = {
            .name = "--opt2",
        };

        struct cli_error error = {0};
        cli_parse_args(
            program_description,
            2,
            (struct cli_param*[]){&param1, &param2},
            3,
            (const char*[]){program_name, "--opt2", "2"},
            &error
        );

        TEST_ASSERT(error.code == CLI_CODE_FAILURE);
        assert_error_contains(&error, "--opt1");
        assert_error_contains(&error, "missing");
        assert_error_contains(&error, "required");
    }

    // option missing value
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
        };

        struct cli_error error = {0};
        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param1},
            2,
            (const char*[]){program_name, "--opt1"},
            &error
        );

        TEST_ASSERT(error.code == CLI_CODE_FAILURE);
        assert_error_contains(&error, param1.name);
        assert_error_contains(&error, "no value specified");
    }

    // flag type option is given
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
            .type = CLI_FLAG,
        };

        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param1},
            2,
            (const char*[]){program_name, "--opt1"},
            NULL
        );

        TEST_ASSERT(param1.value.present);
    }

    // flag type option is not given
    //
    {
        struct cli_param param1 = {
            .name = "--opt1",
            .type = CLI_FLAG,
        };

        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param1},
            1,
            (const char*[]){program_name},
            NULL
        );

        TEST_ASSERT(!param1.value.present);
    }

    // string validation success
    //
    {
        struct expected_value expected[] = {
            {
                .input     = "abc",
                .value.str = "abc",
            },
            {
                .input     = "1",
                .value.str = "1",
            },
            {
                .input     = "1.23",
                .value.str = "1.23",
            },
        };

        for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
            struct cli_param param = {
                .name = "param1",
                .type = CLI_STR,
            };
            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param},
                2,
                (const char*[]){program_name, expected[i].input},
                NULL
            );

            TEST_ASSERT(strcmp(param.value.str, expected[i].value.str) == 0);
        }
    }

    // int validation success
    //
    {
        struct expected_value expected[] = {
            {
                .input     = "12345",
                .value.i64 = 12345,
            },
            {
                .input     = "0",
                .value.i64 = 0,
            },
            {
                .input     = "-54321",
                .value.i64 = -54321,
            },
        };


        for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
            struct cli_param param = {
                .name = "param1",
                .type = CLI_INT,
            };

            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param},
                2,
                (const char*[]){program_name, expected[i].input},
                NULL
            );

            TEST_ASSERT(param.value.i64 == expected[i].value.i64);
        }
    }

    // int validation failure
    //
    {
        struct cli_param param1 = {
            .name = "param1",
            .type = CLI_INT,
        };

        const char* failing_inputs[] = {
            "0.0",
            "1.0",
            ".1",
            "1.",
            "-0.1",
            "-.1",
            "-1.",
            "abc",
            "123abc",
            "-123abc",
        };

        for (size_t i = 0; i < sizeof failing_inputs / sizeof *failing_inputs; i++) {
            struct cli_error error = {0};
            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param1},
                2,
                (const char*[]){program_name, failing_inputs[i]},
                &error
            );

            TEST_ASSERT(error.code == CLI_CODE_FAILURE);
            assert_error_contains(&error, "param1");
            assert_error_contains(&error, "int");
            assert_error_contains(&error, failing_inputs[i]);
        }
    }

    // float validation success
    //
    {
        struct expected_value expected[] = {
            {
                .input     = "12345",
                .value.f64 = 12345.,
            },
            {
                .input     = "0",
                .value.f64 = 0.,
            },
            {
                .input     = "-54321",
                .value.f64 = -54321.,
            },
            {
                .input     = "-1.23",
                .value.f64 = -1.23,
            },
            {
                .input     = "1.23",
                .value.f64 = 1.23,
            },
            {
                .input     = "0.0",
                .value.f64 = 0.0,
            },
            {
                .input     = "1.",
                .value.f64 = 1.,
            },
            {
                .input     = "-1.",
                .value.f64 = -1.,
            },
        };


        for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
            struct cli_param param = {
                .name = "param1",
                .type = CLI_FLOAT,
            };

            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param},
                2,
                (const char*[]){program_name, expected[i].input},
                NULL
            );

            assert_f64_equal(param.value.f64, expected[i].value.f64);
        }
    }

    // float validation failure
    //
    {
        struct cli_param param1 = {
            .name = "param1",
            .type = CLI_FLOAT,
        };

        const char* failing_inputs[] = {
            "abc",
            "123.abc",
            "-123.abc",
        };

        for (size_t i = 0; i < sizeof failing_inputs / sizeof *failing_inputs; i++) {
            struct cli_error error = {0};
            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param1},
                2,
                (const char*[]){program_name, failing_inputs[i]},
                &error
            );

            TEST_ASSERT(error.code == CLI_CODE_FAILURE);
            assert_error_contains(&error, "param1");
            assert_error_contains(&error, "float");
            assert_error_contains(&error, failing_inputs[i]);
        }
    }

    printf("%s tests passed\n", __FILE__);
    return 0;
}

#endif  // CLI_TEST_MAIN
