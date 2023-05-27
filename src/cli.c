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

#define CLI_WRITE_ERROR(error, message) CLI_WRITE_ERRORF(error, "%s", message);

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

struct info_printer {
    char* buffer;
    int   remaining;
};

#define INFO_PRINTF(info_printer, fmt, ...)                                                        \
    do {                                                                                           \
        if ((info_printer)->remaining <= 0) {                                                      \
            break;                                                                                 \
        }                                                                                          \
        int written =                                                                              \
            snprintf((info_printer)->buffer, (info_printer)->remaining, fmt, __VA_ARGS__);         \
        (info_printer)->remaining -= written;                                                      \
        (info_printer)->buffer += written;                                                         \
    } while (0)

#define INFO_PRINT(info_printer, msg) INFO_PRINTF(info_printer, "%s", msg)

static void
print_param_choices(struct info_printer* printer, const struct cli_param* param)
{
    CLI_ASSERT(param);
    CLI_ASSERT(param->validation.strategy == CLI_VALIDATION_CHOICES);

    INFO_PRINT(printer, "{");
    for (size_t i = 0; i < param->validation.choices.count; i++) {
        if (i > 0) {
            INFO_PRINT(printer, ", ");
        }
        INFO_PRINT(printer, param->validation.choices.values[i]);
    }
    INFO_PRINT(printer, "}");
}

static void
print_param_docs(struct info_printer* printer, const struct cli_param* param)
{
    INFO_PRINTF(
        printer,
        "\t%s (%s) - %s\n",
        param->name,
        cli_type_to_cstr(param->type),
        (param->description) ? param->description : "no description"
    );
    switch (param->validation.strategy) {
        case CLI_VALIDATION_TYPES_ONLY:
            break;
        case CLI_VALIDATION_RANGE:
            break;
        case CLI_VALIDATION_CHOICES:
            INFO_PRINT(printer, "\t  ");
            print_param_choices(printer, param);
            INFO_PRINT(printer, "\n");
            break;
    }
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
    char                usage_buffer[sizeof error->reason];
    struct info_printer printer = {
        .buffer    = usage_buffer,
        .remaining = sizeof usage_buffer,
    };
    int options_count = 0;

    INFO_PRINTF(&printer, "%s: %s\n\n", program_name, program_description);
    INFO_PRINTF(&printer, "%s:\n", "positional arguments");

    for (size_t i = 0; i < params_count; i++) {
        if (params[i]->name[0] == '-') {
            options_count += 1;
            continue;
        }
        print_param_docs(&printer, params[i]);
    }
    INFO_PRINTF(&printer, "%s", "\n");

    if (options_count) {
        INFO_PRINTF(&printer, "%s:\n", "options");
        for (size_t i = 0; i < params_count; i++) {
            if (params[i]->name[0] == '-') {
                print_param_docs(&printer, params[i]);
            }
        }
    }

    CLI_WRITE_ERROR(error, usage_buffer);
}

static void
cli_param_parse_input_value(const char* input, struct cli_param* param, struct cli_error* error)
{
    CLI_ASSERT(param);

    if (!input || *input == '\0') {
        CLI_WRITE_ERRORF(error, "invalid input value (empty) for param `%s`", param->name);
    }

    switch (param->type) {
        case CLI_FLAG:
            CLI_ASSERT(0 && "unreachable");
            break;
        case CLI_STR:
            param->value.str = input;
            break;
        case CLI_INT: {
            char* endptr;
            param->value.i64 = strtoll(input, &endptr, 10);
            if (*endptr != '\0') {
                CLI_WRITE_ERRORF(
                    error,
                    "expecting %s type for param `%s` but got value `%s`",
                    cli_type_to_cstr(param->type),
                    param->name,
                    input
                );
                return;
            }
            break;
        }
        case CLI_FLOAT: {
            char* endptr;
            param->value.f64 = strtod(input, &endptr);
            if (*endptr != '\0') {
                CLI_WRITE_ERRORF(
                    error,
                    "expecting %s type for param `%s` but got value `%s`",
                    cli_type_to_cstr(param->type),
                    param->name,
                    input
                );
                return;
            }
            break;
        }
    }

    switch (param->validation.strategy) {
        case CLI_VALIDATION_TYPES_ONLY:
            break;
        case CLI_VALIDATION_CHOICES: {
            bool valid = false;
            for (size_t i = 0; i < param->validation.choices.count; i++) {
                if (strcmp(input, param->validation.choices.values[i]) == 0) {
                    valid = true;
                    break;
                }
            }
            if (!valid) {
                char                error_buffer[sizeof error->reason];
                struct info_printer printer = {
                    .buffer = error_buffer, .remaining = sizeof error_buffer};
                INFO_PRINTF(
                    &printer, "value (%s) given for param `%s` not in choices ", input, param->name
                );
                print_param_choices(&printer, param);
                CLI_WRITE_ERROR(error, error_buffer);
            }
            break;
        }
        case CLI_VALIDATION_RANGE:
            break;
    }
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
                    cli_param_parse_input_value(argv[arg], params[param], error);
                    if (CLI_ERROR_IS_SET(error)) {
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
            cli_param_parse_input_value(argv[arg], params[param], error);
            if (CLI_ERROR_IS_SET(error)) {
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
