#include "script.h"
#include "aegisfs.h"
#include "string.h"
#include "vga.h"

#define SCRIPT_MAX_VARIABLES 32
#define SCRIPT_NAME_SIZE     32
#define SCRIPT_STRING_SIZE   128

typedef enum {
    VALUE_STRING,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_BOOL,
    VALUE_CHAR
} value_type_t;

typedef struct {
    value_type_t type;
    union {
        char string[SCRIPT_STRING_SIZE];
        int32_t integer;
        int32_t float_milli;
        int boolean;
        char character;
    } data;
} value_t;

typedef struct {
    char name[SCRIPT_NAME_SIZE];
    value_t value;
} variable_t;

static uint8_t script_buffer[FS_MAX_SIZE];
static variable_t variables[SCRIPT_MAX_VARIABLES];
static uint32_t variable_count;

enum {
    PARSE_INVALID_STATEMENT = -1,
    PARSE_EXPECTED_VALUE = -2,
    PARSE_INVALID_ESCAPE = -3,
    PARSE_UNTERMINATED_STRING = -4,
    PARSE_TRAILING_TEXT = -5,
    PARSE_INVALID_NAME = -6,
    PARSE_UNKNOWN_VARIABLE = -7,
    PARSE_UNKNOWN_TYPE = -8,
    PARSE_TYPE_MISMATCH = -9,
    PARSE_INVALID_NUMBER = -10,
    PARSE_DUPLICATE_VARIABLE = -11,
    PARSE_TOO_MANY_VARIABLES = -12,
    PARSE_VALUE_TOO_LONG = -13
};

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static void skip_space(const uint8_t *line, uint32_t length, uint32_t *index) {
    while (*index < length && is_space((char)line[*index]))
        (*index)++;
}

static int is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static int parse_name(const uint8_t *line, uint32_t length, uint32_t *index,
                      char name[SCRIPT_NAME_SIZE], uint32_t *error_column) {
    if (*index >= length || !is_name_start((char)line[*index])) {
        *error_column = *index + 1;
        return -1;
    }

    uint32_t name_start = *index;
    while (*index < length && is_name_char((char)line[*index]))
        (*index)++;

    uint32_t name_length = *index - name_start;
    if (name_length >= SCRIPT_NAME_SIZE) {
        *error_column = name_start + 1;
        return -1;
    }
    memcpy(name, line + name_start, name_length);
    name[name_length] = '\0';
    return 0;
}

static int find_variable(const char *name) {
    for (uint32_t i = 0; i < variable_count; i++) {
        if (strcmp(variables[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int parse_escape(const uint8_t *line, uint32_t length, uint32_t *index,
                        char *value, uint32_t *error_column) {
    if (*index >= length) {
        *error_column = *index + 1;
        return -1;
    }
    char c = (char)line[(*index)++];
    if (c == 'n')
        *value = '\n';
    else if (c == 't')
        *value = '\t';
    else if (c == '\\' || c == '"' || c == '\'')
        *value = c;
    else {
        *error_column = *index;
        return -1;
    }
    return 0;
}

static int parse_string_literal(const uint8_t *line, uint32_t length,
                                uint32_t *index, value_t *value,
                                uint32_t *error_column) {
    (*index)++;
    value->type = VALUE_STRING;
    uint32_t output_length = 0;
    int closed = 0;

    while (*index < length) {
        char c = (char)line[(*index)++];
        if (c == '"') {
            closed = 1;
            break;
        }
        if (c == '\\' &&
            parse_escape(line, length, index, &c, error_column) != 0)
            return PARSE_INVALID_ESCAPE;
        if (output_length >= SCRIPT_STRING_SIZE - 1) {
            *error_column = *index;
            return PARSE_VALUE_TOO_LONG;
        }
        value->data.string[output_length++] = c;
    }

    if (!closed) {
        *error_column = length + 1;
        return PARSE_UNTERMINATED_STRING;
    }
    value->data.string[output_length] = '\0';
    return 0;
}

static int parse_char_literal(const uint8_t *line, uint32_t length,
                              uint32_t *index, value_t *value,
                              uint32_t *error_column) {
    (*index)++;
    if (*index >= length) {
        *error_column = length + 1;
        return PARSE_UNTERMINATED_STRING;
    }
    char c = (char)line[(*index)++];
    if (c == '\\' &&
        parse_escape(line, length, index, &c, error_column) != 0)
        return PARSE_INVALID_ESCAPE;
    if (*index >= length || line[*index] != '\'') {
        *error_column = *index + 1;
        return PARSE_EXPECTED_VALUE;
    }
    (*index)++;
    value->type = VALUE_CHAR;
    value->data.character = c;
    return 0;
}

static int parse_number(const uint8_t *line, uint32_t length, uint32_t *index,
                        value_t *value, uint32_t *error_column) {
    uint32_t start = *index;
    int negative = 0;
    if (*index < length && (line[*index] == '-' || line[*index] == '+')) {
        negative = line[*index] == '-';
        (*index)++;
    }

    uint32_t integer_part = 0;
    uint32_t integer_digits = 0;
    while (*index < length && line[*index] >= '0' && line[*index] <= '9') {
        uint32_t digit = line[(*index)++] - '0';
        if (integer_part > 214748364u ||
            (integer_part == 214748364u && digit > 8u)) {
            *error_column = start + 1;
            return PARSE_INVALID_NUMBER;
        }
        integer_part = integer_part * 10 + digit;
        integer_digits++;
    }

    int is_float = 0;
    uint32_t fractional_part = 0;
    uint32_t fractional_digits = 0;
    if (*index < length && line[*index] == '.') {
        is_float = 1;
        (*index)++;
        while (*index < length && line[*index] >= '0' && line[*index] <= '9') {
            if (fractional_digits == 3) {
                *error_column = *index + 1;
                return PARSE_INVALID_NUMBER;
            }
            fractional_part = fractional_part * 10 + (line[(*index)++] - '0');
            fractional_digits++;
        }
        if (!fractional_digits) {
            *error_column = *index + 1;
            return PARSE_INVALID_NUMBER;
        }
    }

    if (!integer_digits || (*index < length && !is_space((char)line[*index]) &&
                            line[*index] != '#')) {
        *error_column = *index + 1;
        return PARSE_INVALID_NUMBER;
    }

    if (is_float) {
        while (fractional_digits++ < 3)
            fractional_part *= 10;
        uint32_t max_fraction = negative ? 648u : 647u;
        if (integer_part > 2147483u ||
            (integer_part == 2147483u && fractional_part > max_fraction)) {
            *error_column = start + 1;
            return PARSE_INVALID_NUMBER;
        }
        uint32_t magnitude = integer_part * 1000 + fractional_part;
        value->type = VALUE_FLOAT;
        if (negative)
            value->data.float_milli = magnitude == 2147483648u
                ? (int32_t)0x80000000u : -(int32_t)magnitude;
        else
            value->data.float_milli = (int32_t)magnitude;
    } else {
        if (integer_part > 2147483647u + (uint32_t)negative) {
            *error_column = start + 1;
            return PARSE_INVALID_NUMBER;
        }
        value->type = VALUE_INT;
        if (negative)
            value->data.integer = integer_part == 2147483648u
                ? (int32_t)0x80000000u : -(int32_t)integer_part;
        else
            value->data.integer = (int32_t)integer_part;
    }
    return 0;
}

static int parse_literal(const uint8_t *line, uint32_t length, uint32_t *index,
                         value_t *value, uint32_t *error_column) {
    memset(value, 0, sizeof(*value));
    if (*index >= length) {
        *error_column = *index + 1;
        return PARSE_EXPECTED_VALUE;
    }
    if (line[*index] == '"')
        return parse_string_literal(line, length, index, value, error_column);
    if (line[*index] == '\'')
        return parse_char_literal(line, length, index, value, error_column);
    if ((line[*index] >= '0' && line[*index] <= '9') ||
        line[*index] == '-' || line[*index] == '+')
        return parse_number(line, length, index, value, error_column);

    if (length - *index >= 4 && line[*index] == 't' &&
        line[*index + 1] == 'r' && line[*index + 2] == 'u' &&
        line[*index + 3] == 'e') {
        *index += 4;
        value->type = VALUE_BOOL;
        value->data.boolean = 1;
        return 0;
    }
    if (length - *index >= 5 && line[*index] == 'f' &&
        line[*index + 1] == 'a' && line[*index + 2] == 'l' &&
        line[*index + 3] == 's' && line[*index + 4] == 'e') {
        *index += 5;
        value->type = VALUE_BOOL;
        value->data.boolean = 0;
        return 0;
    }
    *error_column = *index + 1;
    return PARSE_EXPECTED_VALUE;
}

static int parse_type_name(const uint8_t *line, uint32_t length,
                           uint32_t *index, value_type_t *type,
                           uint32_t *error_column) {
    char name[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, index, name, error_column) != 0)
        return PARSE_UNKNOWN_TYPE;
    if (strcmp(name, "String") == 0)
        *type = VALUE_STRING;
    else if (strcmp(name, "Int") == 0)
        *type = VALUE_INT;
    else if (strcmp(name, "Float") == 0)
        *type = VALUE_FLOAT;
    else if (strcmp(name, "Bool") == 0)
        *type = VALUE_BOOL;
    else if (strcmp(name, "Char") == 0)
        *type = VALUE_CHAR;
    else {
        *error_column = *index + 1;
        return PARSE_UNKNOWN_TYPE;
    }
    return 0;
}

static void print_value(const value_t *value) {
    switch (value->type) {
        case VALUE_STRING:
            for (const char *p = value->data.string; *p; p++) {
                vga_putchar(*p);
                if (*p == '\n')
                    vga_putchar('\r');
            }
            break;
        case VALUE_INT: {
            int32_t integer = value->data.integer;
            uint32_t magnitude = integer < 0
                ? (uint32_t)(-(integer + 1)) + 1 : (uint32_t)integer;
            if (integer < 0)
                vga_putchar('-');
            vga_printf("%u", magnitude);
            break;
        }
        case VALUE_FLOAT: {
            int32_t number = value->data.float_milli;
            uint32_t magnitude = number < 0
                ? (uint32_t)(-(number + 1)) + 1 : (uint32_t)number;
            if (number < 0)
                vga_putchar('-');
            vga_printf("%u.%03u", magnitude / 1000, magnitude % 1000);
            break;
        }
        case VALUE_BOOL:
            vga_print(value->data.boolean ? "true" : "false");
            break;
        case VALUE_CHAR:
            vga_putchar(value->data.character);
            if (value->data.character == '\n')
                vga_putchar('\r');
            break;
    }
    vga_putchar('\n');
}

static int statement_end(const uint8_t *line, uint32_t length, uint32_t index,
                        uint32_t *error_column) {
    skip_space(line, length, &index);
    if (index < length && line[index] != '#') {
        *error_column = index + 1;
        return PARSE_TRAILING_TEXT;
    }
    return 0;
}

static int parse_print(const uint8_t *line, uint32_t length, uint32_t index,
                       uint32_t *error_column) {
    value_t value;
    int status;
    skip_space(line, length, &index);
    if (index < length && line[index] == '"') {
        status = parse_string_literal(line, length, &index, &value,
                                      error_column);
    } else {
        char name[SCRIPT_NAME_SIZE];
        if (parse_name(line, length, &index, name, error_column) != 0)
            return PARSE_EXPECTED_VALUE;
        int variable = find_variable(name);
        if (variable < 0) {
            *error_column = index - strlen(name) + 1;
            return PARSE_UNKNOWN_VARIABLE;
        }
        value = variables[variable].value;
        status = 0;
    }
    if (status != 0)
        return status;
    status = statement_end(line, length, index, error_column);
    if (status != 0)
        return status;
    print_value(&value);
    return 1;
}

static int parse_let(const uint8_t *line, uint32_t length, uint32_t index,
                     uint32_t *error_column) {
    char name[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, &index, name, error_column) != 0)
        return PARSE_INVALID_NAME;
    if (find_variable(name) >= 0) {
        *error_column = index - strlen(name) + 1;
        return PARSE_DUPLICATE_VARIABLE;
    }

    skip_space(line, length, &index);
    int has_annotation = 0;
    value_type_t annotated_type = VALUE_STRING;
    if (index < length && line[index] == ':') {
        has_annotation = 1;
        index++;
        skip_space(line, length, &index);
        int status = parse_type_name(line, length, &index, &annotated_type,
                                     error_column);
        if (status != 0)
            return status;
        skip_space(line, length, &index);
    }
    if (index >= length || line[index] != '=') {
        *error_column = index + 1;
        return PARSE_EXPECTED_VALUE;
    }
    index++;
    skip_space(line, length, &index);

    value_t value;
    int status = parse_literal(line, length, &index, &value, error_column);
    if (status != 0)
        return status;
    status = statement_end(line, length, index, error_column);
    if (status != 0)
        return status;
    if (has_annotation && annotated_type != value.type) {
        *error_column = index + 1;
        return PARSE_TYPE_MISMATCH;
    }
    if (variable_count >= SCRIPT_MAX_VARIABLES)
        return PARSE_TOO_MANY_VARIABLES;

    strcpy(variables[variable_count].name, name);
    variables[variable_count].value = value;
    variable_count++;
    return 1;
}

static int parse_line(const uint8_t *line, uint32_t length,
                      uint32_t *error_column) {
    uint32_t index = 0;
    skip_space(line, length, &index);
    if (index == length || line[index] == '#')
        return 0;

    char keyword[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, &index, keyword, error_column) != 0)
        return PARSE_INVALID_STATEMENT;
    skip_space(line, length, &index);
    if (strcmp(keyword, "print") == 0)
        return parse_print(line, length, index, error_column);
    if (strcmp(keyword, "let") == 0)
        return parse_let(line, length, index, error_column);
    *error_column = 1;
    return PARSE_INVALID_STATEMENT;
}

static const char *parse_error_message(int status) {
    switch (status) {
        case PARSE_EXPECTED_VALUE: return "expected a value";
        case PARSE_INVALID_ESCAPE: return "invalid escape sequence";
        case PARSE_UNTERMINATED_STRING: return "unterminated string or character";
        case PARSE_TRAILING_TEXT: return "unexpected text after statement";
        case PARSE_INVALID_NAME: return "invalid variable name";
        case PARSE_UNKNOWN_VARIABLE: return "unknown variable";
        case PARSE_UNKNOWN_TYPE: return "unknown type name";
        case PARSE_TYPE_MISMATCH: return "value does not match declared type";
        case PARSE_INVALID_NUMBER: return "invalid number";
        case PARSE_DUPLICATE_VARIABLE: return "variable is already declared";
        case PARSE_TOO_MANY_VARIABLES: return "variable limit reached";
        case PARSE_VALUE_TOO_LONG: return "string exceeds the 127 character limit";
        default: return "invalid statement";
    }
}

int script_run_file(const char *filename) {
    uint32_t size = 0;
    if (fs_read(filename, script_buffer, &size) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "run: cannot read script: %s\n", filename);
        return -1;
    }

    memset(variables, 0, sizeof(variables));
    variable_count = 0;

    uint32_t line_number = 1;
    uint32_t line_start = 0;
    while (line_start < size) {
        uint32_t line_end = line_start;
        while (line_end < size && script_buffer[line_end] != '\n')
            line_end++;

        uint32_t error_column = 0;
        int status = parse_line(script_buffer + line_start,
                                line_end - line_start, &error_column);
        if (status < 0) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "run: %s:%u:%u: %s\n",
                              filename, line_number, error_column,
                              parse_error_message(status));
            return -1;
        }

        line_start = line_end + (line_end < size ? 1 : 0);
        line_number++;
    }
    return 0;
}
