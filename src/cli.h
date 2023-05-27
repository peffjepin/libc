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
            // both inclusive
            union cli_value start;
            union cli_value stop;
        } range;

        struct {
            size_t       count;
            const char** values;
        } choices;
    };
};

enum cli_flags {
    CLI_FLAG_OPTION_REQUIRED = 1u << 0,
};

struct cli_param {
    const char*           name;
    const char*           description;
    enum cli_type         type;
    uint32_t              flags;
    struct cli_validation validation;
    union cli_value       value;
};

enum cli_code {
    CLI_CODE_SUCCESS = 0,
    CLI_CODE_WARNING,
    CLI_CODE_FAILURE,
};

struct cli_error {
    enum cli_code code;
    char          reason[1024];
};

void cli_parse_args(
    const char*        program_description,
    size_t             params_count,
    struct cli_param** params,
    int                argc,
    const char**       argv,
    struct cli_error*  error
);

#ifdef CLI_TEST_MAIN

#ifndef TEST_ASSERT
#include <assert.h>
#define TEST_ASSERT assert
#endif  // TEST_ASSERT

#include <stdio.h>
#include <string.h>

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
        struct cli_param param3 = {
            .name        = "param3",
            .description = "string choices",
            .type        = CLI_STR,
            .validation =
                {
                    .strategy = CLI_VALIDATION_CHOICES,
                    .choices =
                        {
                            .count  = 2,
                            .values = (const char*[]){"choice1", "choice2"},
                        },
                },
        };
        struct cli_param param4 = {
            .name        = "param4",
            .description = "float choices",
            .type        = CLI_FLOAT,
            .validation =
                {
                    .strategy = CLI_VALIDATION_CHOICES,
                    .choices =
                        {
                            .count  = 2,
                            .values = (const char*[]){"123.0", "321.0"},
                        },
                },
        };
        struct cli_param param5 = {
            .name        = "param5",
            .description = "int range",
            .type        = CLI_INT,
            .validation =
                {
                    .strategy = CLI_VALIDATION_RANGE,
                    .range =
                        {
                            .start.i64 = 0,
                            .stop.i64  = 10,
                        },
                },
        };

        const char* help_options[] = {"-help", "--help"};
        for (size_t i = 0; i < sizeof help_options / sizeof *help_options; i++) {
            struct cli_error error = {0};
            cli_parse_args(
                program_description,
                5,
                (struct cli_param*[]){&param1, &param2, &param3, &param4, &param5},
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

            assert_error_contains(&error, param3.name);
            assert_error_contains(&error, param3.description);
            assert_error_contains(&error, "choice1");
            assert_error_contains(&error, "choice2");

            assert_error_contains(&error, param4.name);
            assert_error_contains(&error, param4.description);
            assert_error_contains(&error, "123");
            assert_error_contains(&error, "321");

            assert_error_contains(&error, param5.name);
            assert_error_contains(&error, param5.description);
            assert_error_contains(&error, "[0-10]");
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

    // argument not present in choices
    //
    {
        struct cli_param param = {
            .name = "param1",
            .type = CLI_STR,
            .validation =
                {
                    .strategy = CLI_VALIDATION_CHOICES,
                    .choices =
                        {
                            .count  = 2,
                            .values = (const char*[]){"choice1", "choice2"},
                        },
                },
        };

        struct cli_error error = {0};
        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param},
            2,
            (const char*[]){program_name, "choice3"},
            &error
        );

        TEST_ASSERT(error.code == CLI_CODE_FAILURE);
        assert_error_contains(&error, "choice1");
        assert_error_contains(&error, "choice2");
        assert_error_contains(&error, "choice3");
    }

    // argument is present in choices
    //
    {
        struct cli_param param = {
            .name = "param1",
            .type = CLI_INT,
            .validation =
                {
                    .strategy = CLI_VALIDATION_CHOICES,
                    .choices =
                        {
                            .count  = 2,
                            .values = (const char*[]){"123", "456"},
                        },
                },
        };

        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param},
            2,
            (const char*[]){program_name, "456"},
            NULL
        );

        TEST_ASSERT(param.value.i64 == 456);
    }

    // range validation
    //
    {
        struct cli_param param = {
            .name = "param1",
            .type = CLI_INT,
            .validation =
                {
                    .strategy = CLI_VALIDATION_RANGE,
                    .range =
                        {
                            .start.i64 = 0,
                            .stop.i64  = 10,
                        },
                },
        };

        const char* out_of_range_values[] = {"-1", "11", "100"};
        for (size_t i = 0; i < sizeof out_of_range_values / sizeof *out_of_range_values; i++) {
            struct cli_error error = {0};
            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param},
                2,
                (const char*[]){program_name, out_of_range_values[i]},
                &error
            );
            assert_error_contains(&error, out_of_range_values[i]);
            assert_error_contains(&error, "[0-10]");
        }

        struct expected_value in_range_values[] = {
            {.input = "0", .value.i64 = 0},
            {.input = "10", .value.i64 = 10},
            {.input = "5", .value.i64 = 5},
        };
        for (size_t i = 0; i < sizeof in_range_values / sizeof *in_range_values; i++) {
            cli_parse_args(
                program_description,
                1,
                (struct cli_param*[]){&param},
                2,
                (const char*[]){program_name, in_range_values[i].input},
                NULL
            );
            TEST_ASSERT(param.value.i64 == in_range_values[i].value.i64);
        }
    }

    // unused args
    {
        struct cli_param param = {
            .name = "param1",
        };

        struct cli_error error = {0};
        cli_parse_args(
            program_description,
            1,
            (struct cli_param*[]){&param},
            4,
            (const char*[]){program_name, "abc", "def", "zzz"},
            &error
        );
        TEST_ASSERT(error.code == CLI_CODE_WARNING);

        assert_error_contains(&error, "unused");
        assert_error_contains(&error, "def");
        assert_error_contains(&error, "zzz");
    }

    printf("%s tests passed\n", __FILE__);
    return 0;
}

#endif  // CLI_TEST_MAIN

#endif  // CLI_H
