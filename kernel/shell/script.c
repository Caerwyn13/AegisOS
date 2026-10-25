#include "script.h"
#include "aegisfs.h"
#include "string.h"
#include "vga.h"

#define SCRIPT_MAX_VARIABLES 32
#define SCRIPT_NAME_SIZE     32
#define SCRIPT_STRING_SIZE   128
#define SCRIPT_FORMAT_SIZE   512
#define SCRIPT_MAX_EXPRESSION_DEPTH 32
#define SCRIPT_MAX_BLOCK_DEPTH 32
#define SCRIPT_MAX_EXECUTED_LINES 10000
#define SCRIPT_NO_JUMP 0xFFFFFFFFu

typedef enum {
    VALUE_STRING,
    VALUE_INT,
    VALUE_FLOAT,
    VALUE_BOOL,
    VALUE_CHAR
} value_type_t;

typedef enum {
    BLOCK_IF,
    BLOCK_WHILE
} block_type_t;

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

typedef struct {
    block_type_t type;
    int parent_active;
    int condition;
    int has_else;
    uint32_t condition_offset;
    uint32_t condition_length;
    uint32_t body_offset;
    uint32_t body_line_number;
    uint32_t while_line_number;
} script_block_t;

static uint8_t script_buffer[FS_MAX_SIZE];
static variable_t variables[SCRIPT_MAX_VARIABLES];
static char formatted_output[SCRIPT_FORMAT_SIZE];
static uint32_t variable_count;
static uint32_t expression_depth;

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
    PARSE_VALUE_TOO_LONG = -13,
    PARSE_INVALID_FORMAT = -14,
    PARSE_OUTPUT_TOO_LONG = -15,
    PARSE_NON_NUMERIC = -16,
    PARSE_DIVISION_BY_ZERO = -17,
    PARSE_ARITHMETIC_OVERFLOW = -18,
    PARSE_ASSIGNMENT_TYPE_MISMATCH = -19,
    PARSE_RESERVED_NAME = -20,
    PARSE_EXPRESSION_TOO_DEEP = -21,
    PARSE_CONDITION_NOT_BOOL = -22,
    PARSE_INVALID_COMPARISON = -23,
    PARSE_INVALID_BLOCK = -24,
    PARSE_BLOCK_TOO_DEEP = -25,
    PARSE_UNCLOSED_BLOCK = -26,
    PARSE_COMPARISON_TYPE_MISMATCH = -27,
    PARSE_EXECUTION_LIMIT = -28
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

static int is_reserved_name(const char *name) {
    return strcmp(name, "let") == 0 || strcmp(name, "print") == 0 ||
           strcmp(name, "true") == 0 || strcmp(name, "false") == 0 ||
           strcmp(name, "if") == 0 || strcmp(name, "else") == 0 ||
           strcmp(name, "end") == 0 || strcmp(name, "while") == 0;
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

static int append_char(char *output, uint32_t *length, char value) {
    if (*length >= SCRIPT_FORMAT_SIZE - 1)
        return -1;
    output[(*length)++] = value;
    output[*length] = '\0';
    return 0;
}

static int append_text(char *output, uint32_t *length, const char *text) {
    while (*text) {
        if (append_char(output, length, *text++) != 0)
            return -1;
    }
    return 0;
}

static int append_unsigned(char *output, uint32_t *length, uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (count) {
        if (append_char(output, length, digits[--count]) != 0)
            return -1;
    }
    return 0;
}

static int append_value(char *output, uint32_t *length,
                        const value_t *value) {
    switch (value->type) {
        case VALUE_STRING:
            return append_text(output, length, value->data.string);
        case VALUE_INT: {
            int32_t number = value->data.integer;
            uint32_t magnitude = number < 0
                ? (uint32_t)(-(number + 1)) + 1 : (uint32_t)number;
            if (number < 0 && append_char(output, length, '-') != 0)
                return -1;
            return append_unsigned(output, length, magnitude);
        }
        case VALUE_FLOAT: {
            int32_t number = value->data.float_milli;
            uint32_t magnitude = number < 0
                ? (uint32_t)(-(number + 1)) + 1 : (uint32_t)number;
            if (number < 0 && append_char(output, length, '-') != 0)
                return -1;
            if (append_unsigned(output, length, magnitude / 1000) != 0 ||
                append_char(output, length, '.') != 0)
                return -1;
            uint32_t fraction = magnitude % 1000;
            if (append_char(output, length, (char)('0' + fraction / 100)) != 0 ||
                append_char(output, length, (char)('0' + (fraction / 10) % 10)) != 0 ||
                append_char(output, length, (char)('0' + fraction % 10)) != 0)
                return -1;
            return 0;
        }
        case VALUE_BOOL:
            return append_text(output, length,
                               value->data.boolean ? "true" : "false");
        case VALUE_CHAR:
            return append_char(output, length, value->data.character);
    }
    return -1;
}

static void print_text(const char *text) {
    while (*text) {
        vga_putchar(*text);
        if (*text++ == '\n')
            vga_putchar('\r');
    }
}

static int format_string(const char *input, uint32_t *error_column) {
    uint32_t output_length = 0;
    formatted_output[0] = '\0';

    for (uint32_t i = 0; input[i];) {
        if (input[i] == '{' && input[i + 1] == '{') {
            if (append_char(formatted_output, &output_length, '{') != 0)
                return PARSE_OUTPUT_TOO_LONG;
            i += 2;
            continue;
        }
        if (input[i] == '}' && input[i + 1] == '}') {
            if (append_char(formatted_output, &output_length, '}') != 0)
                return PARSE_OUTPUT_TOO_LONG;
            i += 2;
            continue;
        }
        if (input[i] == '{') {
            uint32_t name_start = i + 1;
            uint32_t name_end = name_start;
            while (input[name_end] && input[name_end] != '}')
                name_end++;
            if (!input[name_end] || name_end == name_start) {
                *error_column = i + 1;
                return PARSE_INVALID_FORMAT;
            }

            char name[SCRIPT_NAME_SIZE];
            uint32_t name_index = name_start;
            if (parse_name((const uint8_t *)input, name_end, &name_index,
                           name, error_column) != 0 ||
                name_index != name_end) {
                *error_column = name_start + 1;
                return PARSE_INVALID_FORMAT;
            }
            int variable = find_variable(name);
            if (variable < 0) {
                *error_column = name_start + 1;
                return PARSE_UNKNOWN_VARIABLE;
            }
            if (append_value(formatted_output, &output_length,
                             &variables[variable].value) != 0)
                return PARSE_OUTPUT_TOO_LONG;
            i = name_end + 1;
            continue;
        }
        if (input[i] == '}') {
            *error_column = i + 1;
            return PARSE_INVALID_FORMAT;
        }
        if (append_char(formatted_output, &output_length, input[i++]) != 0)
            return PARSE_OUTPUT_TOO_LONG;
    }
    return 0;
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

    if (!integer_digits) {
        *error_column = *index + 1;
        return PARSE_INVALID_NUMBER;
    }

    if (is_float) {
        while (fractional_digits++ < 3)
            fractional_part *= 10;
        uint32_t max_fraction = 647u;
        if (integer_part > 2147483u ||
            (integer_part == 2147483u && fractional_part > max_fraction)) {
            *error_column = start + 1;
            return PARSE_INVALID_NUMBER;
        }
        uint32_t magnitude = integer_part * 1000 + fractional_part;
        value->type = VALUE_FLOAT;
        value->data.float_milli = (int32_t)magnitude;
    } else {
        if (integer_part > 2147483647u) {
            *error_column = start + 1;
            return PARSE_INVALID_NUMBER;
        }
        value->type = VALUE_INT;
        value->data.integer = (int32_t)integer_part;
    }
    return 0;
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

static uint32_t magnitude_i32(int32_t value) {
    return value < 0 ? (uint32_t)(-(value + 1)) + 1 : (uint32_t)value;
}

static int checked_add_i32(int32_t a, int32_t b, int32_t *result) {
    if ((b > 0 && a > 2147483647 - b) ||
        (b < 0 && a < (int32_t)0x80000000u - b))
        return -1;
    *result = a + b;
    return 0;
}

static int checked_sub_i32(int32_t a, int32_t b, int32_t *result) {
    if ((b < 0 && a > 2147483647 + b) ||
        (b > 0 && a < (int32_t)0x80000000u + b))
        return -1;
    *result = a - b;
    return 0;
}

static int checked_mul_i32(int32_t a, int32_t b, int32_t *result) {
    if (!a || !b) {
        *result = 0;
        return 0;
    }
    uint32_t a_abs = magnitude_i32(a);
    uint32_t b_abs = magnitude_i32(b);
    uint32_t limit = ((a < 0) != (b < 0)) ? 2147483648u : 2147483647u;
    if (a_abs > limit / b_abs)
        return -1;
    uint32_t product = a_abs * b_abs;
    if (((a < 0) != (b < 0)) && product == 2147483648u)
        *result = (int32_t)0x80000000u;
    else
        *result = ((a < 0) != (b < 0))
            ? -(int32_t)product : (int32_t)product;
    return 0;
}

static int checked_mul_small_div_1000(uint32_t small, uint32_t value,
                                      uint32_t *result) {
    uint32_t high = value / 1000;
    uint32_t low = value % 1000;
    if (small && high > 2147483647u / small)
        return -1;
    uint32_t whole = small * high;
    uint32_t fraction = (small * low) / 1000;
    if (whole > 2147483647u - fraction)
        return -1;
    *result = whole + fraction;
    return 0;
}

static int float_multiply(int32_t a, int32_t b, int32_t *result) {
    uint32_t a_abs = magnitude_i32(a);
    uint32_t b_abs = magnitude_i32(b);
    uint32_t whole_factor = a_abs / 1000;
    uint32_t whole;
    uint32_t fraction;
    if (whole_factor && b_abs > 2147483647u / whole_factor)
        return -1;
    whole = whole_factor * b_abs;
    if (checked_mul_small_div_1000(a_abs % 1000, b_abs, &fraction) != 0 ||
        whole > 2147483647u - fraction)
        return -1;

    uint32_t product = whole + fraction;
    int negative = (a < 0) != (b < 0);
    if (product > (negative ? 2147483648u : 2147483647u))
        return -1;
    if (negative && product == 2147483648u)
        *result = (int32_t)0x80000000u;
    else
        *result = negative ? -(int32_t)product : (int32_t)product;
    return 0;
}

static int float_divide(int32_t a, int32_t b, int32_t *result) {
    if (!b)
        return PARSE_DIVISION_BY_ZERO;
    uint32_t numerator = magnitude_i32(a);
    uint32_t denominator = magnitude_i32(b);
    uint32_t whole = numerator / denominator;
    uint32_t remainder = numerator % denominator;
    if (whole > 2147483u)
        return -1;

    uint32_t fraction = 0;
    uint32_t remainder_accumulator = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        if (remainder && remainder_accumulator >= denominator - remainder) {
            remainder_accumulator -= denominator - remainder;
            fraction++;
        } else {
            remainder_accumulator += remainder;
        }
    }

    uint32_t magnitude = whole * 1000 + fraction;
    int negative = (a < 0) != (b < 0);
    if (magnitude > (negative ? 2147483648u : 2147483647u))
        return -1;
    if (negative && magnitude == 2147483648u)
        *result = (int32_t)0x80000000u;
    else
        *result = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return 0;
}

static int promote_numeric(value_t *value) {
    if (value->type == VALUE_FLOAT)
        return 0;
    if (value->type != VALUE_INT)
        return PARSE_NON_NUMERIC;
    if (value->data.integer > 2147483 || value->data.integer < -2147483)
        return PARSE_ARITHMETIC_OVERFLOW;
    value->data.float_milli = value->data.integer * 1000;
    value->type = VALUE_FLOAT;
    return 0;
}

static int parse_expression_add(const uint8_t *line, uint32_t length,
                                uint32_t *index, value_t *value,
                                uint32_t *error_column);

static int parse_expression_primary(const uint8_t *line, uint32_t length,
                                   uint32_t *index, value_t *value,
                                   uint32_t *error_column) {
    skip_space(line, length, index);
    memset(value, 0, sizeof(*value));
    if (*index >= length) {
        *error_column = *index + 1;
        return PARSE_EXPECTED_VALUE;
    }
    if (line[*index] == '(') {
        if (expression_depth >= SCRIPT_MAX_EXPRESSION_DEPTH) {
            *error_column = *index + 1;
            return PARSE_EXPRESSION_TOO_DEEP;
        }
        (*index)++;
        expression_depth++;
        int status = parse_expression_add(line, length, index, value,
                                          error_column);
        expression_depth--;
        if (status != 0)
            return status;
        skip_space(line, length, index);
        if (*index >= length || line[*index] != ')') {
            *error_column = *index + 1;
            return PARSE_EXPECTED_VALUE;
        }
        (*index)++;
        return 0;
    }
    if (line[*index] == '"')
        return parse_string_literal(line, length, index, value, error_column);
    if (line[*index] == '\'')
        return parse_char_literal(line, length, index, value, error_column);
    if (line[*index] >= '0' && line[*index] <= '9')
        return parse_number(line, length, index, value, error_column);

    char name[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, index, name, error_column) != 0)
        return PARSE_EXPECTED_VALUE;
    if (strcmp(name, "true") == 0) {
        value->type = VALUE_BOOL;
        value->data.boolean = 1;
        return 0;
    }
    if (strcmp(name, "false") == 0) {
        value->type = VALUE_BOOL;
        value->data.boolean = 0;
        return 0;
    }
    int variable = find_variable(name);
    if (variable < 0) {
        *error_column = *index - strlen(name) + 1;
        return PARSE_UNKNOWN_VARIABLE;
    }
    *value = variables[variable].value;
    return 0;
}

static int parse_expression_unary(const uint8_t *line, uint32_t length,
                                  uint32_t *index, value_t *value,
                                  uint32_t *error_column) {
    int negative = 0;
    skip_space(line, length, index);
    while (*index < length &&
           (line[*index] == '+' || line[*index] == '-')) {
        if (line[*index] == '-')
            negative = !negative;
        (*index)++;
        skip_space(line, length, index);
    }
    int status = parse_expression_primary(line, length, index, value,
                                          error_column);
    if (status != 0 || !negative)
        return status;
    if (value->type == VALUE_INT) {
        if (value->data.integer == (int32_t)0x80000000u)
            return PARSE_ARITHMETIC_OVERFLOW;
        value->data.integer = -value->data.integer;
    } else if (value->type == VALUE_FLOAT) {
        if (value->data.float_milli == (int32_t)0x80000000u)
            return PARSE_ARITHMETIC_OVERFLOW;
        value->data.float_milli = -value->data.float_milli;
    } else {
        return PARSE_NON_NUMERIC;
    }
    return 0;
}

static int apply_numeric_operator(value_t *left, value_t right, char op) {
    if ((left->type != VALUE_INT && left->type != VALUE_FLOAT) ||
        (right.type != VALUE_INT && right.type != VALUE_FLOAT))
        return PARSE_NON_NUMERIC;

    int status;
    if (left->type == VALUE_FLOAT || right.type == VALUE_FLOAT) {
        status = promote_numeric(left);
        if (status != 0)
            return status;
        status = promote_numeric(&right);
        if (status != 0)
            return status;
        int32_t result = 0;
        if (op == '+')
            status = checked_add_i32(left->data.float_milli,
                                     right.data.float_milli, &result);
        else if (op == '-')
            status = checked_sub_i32(left->data.float_milli,
                                     right.data.float_milli, &result);
        else if (op == '*')
            status = float_multiply(left->data.float_milli,
                                    right.data.float_milli, &result);
        else
            status = float_divide(left->data.float_milli,
                                  right.data.float_milli, &result);
        if (status == PARSE_DIVISION_BY_ZERO)
            return status;
        if (status != 0)
            return PARSE_ARITHMETIC_OVERFLOW;
        left->type = VALUE_FLOAT;
        left->data.float_milli = result;
        return 0;
    }

    int32_t result = 0;
    if (op == '+')
        status = checked_add_i32(left->data.integer, right.data.integer, &result);
    else if (op == '-')
        status = checked_sub_i32(left->data.integer, right.data.integer, &result);
    else if (op == '*')
        status = checked_mul_i32(left->data.integer, right.data.integer, &result);
    else {
        if (!right.data.integer)
            return PARSE_DIVISION_BY_ZERO;
        if (left->data.integer == (int32_t)0x80000000u &&
            right.data.integer == -1)
            return PARSE_ARITHMETIC_OVERFLOW;
        result = left->data.integer / right.data.integer;
        left->type = VALUE_INT;
        left->data.integer = result;
        return 0;
    }
    if (status != 0)
        return PARSE_ARITHMETIC_OVERFLOW;
    left->data.integer = result;
    return 0;
}

static int parse_expression_multiply(const uint8_t *line, uint32_t length,
                                     uint32_t *index, value_t *value,
                                     uint32_t *error_column) {
    int status = parse_expression_unary(line, length, index, value,
                                        error_column);
    if (status != 0)
        return status;
    while (1) {
        skip_space(line, length, index);
        if (*index >= length ||
            (line[*index] != '*' && line[*index] != '/'))
            return 0;
        uint32_t operator_column = *index + 1;
        char op = (char)line[(*index)++];
        value_t right;
        status = parse_expression_unary(line, length, index, &right,
                                        error_column);
        if (status != 0)
            return status;
        status = apply_numeric_operator(value, right, op);
        if (status != 0) {
            *error_column = operator_column;
            return status;
        }
    }
}

static int parse_expression_add(const uint8_t *line, uint32_t length,
                                uint32_t *index, value_t *value,
                                uint32_t *error_column) {
    int status = parse_expression_multiply(line, length, index, value,
                                           error_column);
    if (status != 0)
        return status;
    while (1) {
        skip_space(line, length, index);
        if (*index >= length ||
            (line[*index] != '+' && line[*index] != '-'))
            return 0;
        uint32_t operator_column = *index + 1;
        char op = (char)line[(*index)++];
        value_t right;
        status = parse_expression_multiply(line, length, index, &right,
                                           error_column);
        if (status != 0)
            return status;
        status = apply_numeric_operator(value, right, op);
        if (status != 0) {
            *error_column = operator_column;
            return status;
        }
    }
}

static int parse_expression(const uint8_t *line, uint32_t length,
                            uint32_t *index, value_t *value,
                            uint32_t *error_column) {
    return parse_expression_add(line, length, index, value, error_column);
}

static int compare_strings(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    unsigned char left_char = (unsigned char)*left;
    unsigned char right_char = (unsigned char)*right;
    return left_char < right_char ? -1 : left_char > right_char ? 1 : 0;
}

static int compare_int_float(int32_t integer, int32_t float_milli) {
    int32_t float_whole = float_milli / 1000;
    int32_t float_fraction = float_milli % 1000;
    if (integer != float_whole)
        return integer < float_whole ? -1 : 1;
    return float_fraction < 0 ? 1 : float_fraction > 0 ? -1 : 0;
}

static int compare_values(const value_t *left, const value_t *right,
                          char comparison, int *result) {
    int ordering;
    int equality_only = comparison == '=' || comparison == '!';
    int left_numeric = left->type == VALUE_INT || left->type == VALUE_FLOAT;
    int right_numeric = right->type == VALUE_INT || right->type == VALUE_FLOAT;

    if (left_numeric && right_numeric) {
        if (left->type == VALUE_INT && right->type == VALUE_INT) {
            ordering = left->data.integer < right->data.integer ? -1 :
                       left->data.integer > right->data.integer ? 1 : 0;
        } else if (left->type == VALUE_INT) {
            ordering = compare_int_float(left->data.integer,
                                         right->data.float_milli);
        } else if (right->type == VALUE_INT) {
            ordering = -compare_int_float(right->data.integer,
                                          left->data.float_milli);
        } else {
            ordering = left->data.float_milli < right->data.float_milli ? -1 :
                       left->data.float_milli > right->data.float_milli ? 1 : 0;
        }
    } else if (left->type != right->type) {
        return PARSE_COMPARISON_TYPE_MISMATCH;
    } else if (left->type == VALUE_STRING) {
        if (!equality_only)
            return PARSE_COMPARISON_TYPE_MISMATCH;
        ordering = compare_strings(left->data.string, right->data.string);
    } else if (left->type == VALUE_BOOL) {
        if (!equality_only)
            return PARSE_COMPARISON_TYPE_MISMATCH;
        ordering = left->data.boolean < right->data.boolean ? -1 :
                   left->data.boolean > right->data.boolean ? 1 : 0;
    } else if (left->type == VALUE_CHAR) {
        unsigned char left_char = (unsigned char)left->data.character;
        unsigned char right_char = (unsigned char)right->data.character;
        ordering = left_char < right_char ? -1 :
                   left_char > right_char ? 1 : 0;
    } else {
        return PARSE_COMPARISON_TYPE_MISMATCH;
    }

    if (comparison == '=')
        *result = ordering == 0;
    else if (comparison == '!')
        *result = ordering != 0;
    else if (comparison == '<')
        *result = ordering < 0;
    else if (comparison == 'l')
        *result = ordering <= 0;
    else if (comparison == '>')
        *result = ordering > 0;
    else
        *result = ordering >= 0;
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

static int parse_condition(const uint8_t *line, uint32_t length,
                           uint32_t index, int *condition,
                           uint32_t *error_column) {
    uint32_t condition_column = index + 1;
    value_t left;
    int status = parse_expression(line, length, &index, &left, error_column);
    if (status != 0)
        return status;

    skip_space(line, length, &index);
    uint32_t operator_column = index + 1;
    char comparison;
    if (index + 1 < length && line[index] == '=' && line[index + 1] == '=') {
        comparison = '=';
        index += 2;
    } else if (index + 1 < length && line[index] == '!' &&
               line[index + 1] == '=') {
        comparison = '!';
        index += 2;
    } else if (index + 1 < length && line[index] == '<' &&
               line[index + 1] == '=') {
        comparison = 'l';
        index += 2;
    } else if (index + 1 < length && line[index] == '>' &&
               line[index + 1] == '=') {
        comparison = 'g';
        index += 2;
    } else if (index < length && line[index] == '<') {
        comparison = '<';
        index++;
    } else if (index < length && line[index] == '>') {
        comparison = '>';
        index++;
    } else {
        if (index < length && (line[index] == '=' || line[index] == '!')) {
            *error_column = index + 1;
            return PARSE_INVALID_COMPARISON;
        }
        status = statement_end(line, length, index, error_column);
        if (status != 0)
            return status;
        if (left.type != VALUE_BOOL) {
            *error_column = condition_column;
            return PARSE_CONDITION_NOT_BOOL;
        }
        *condition = left.data.boolean;
        return 0;
    }

    value_t right;
    status = parse_expression(line, length, &index, &right, error_column);
    if (status != 0)
        return status;
    status = statement_end(line, length, index, error_column);
    if (status != 0)
        return status;
    status = compare_values(&left, &right, comparison, condition);
    if (status != 0)
        *error_column = operator_column;
    return status;
}

static int parse_print(const uint8_t *line, uint32_t length, uint32_t index,
                       uint32_t *error_column) {
    value_t value;
    int status;
    int formatted = 0;
    skip_space(line, length, &index);
    if (index < length && line[index] == '"') {
        status = parse_string_literal(line, length, &index, &value,
                                      error_column);
        if (status == 0) {
            status = format_string(value.data.string, error_column);
            formatted = status == 0;
        }
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
    if (formatted) {
        print_text(formatted_output);
        vga_putchar('\n');
    } else {
        print_value(&value);
    }
    return 1;
}

static int parse_let(const uint8_t *line, uint32_t length, uint32_t index,
                     uint32_t *error_column) {
    char name[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, &index, name, error_column) != 0)
        return PARSE_INVALID_NAME;
    if (is_reserved_name(name)) {
        *error_column = index - strlen(name) + 1;
        return PARSE_RESERVED_NAME;
    }
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
    int status = parse_expression(line, length, &index, &value, error_column);
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

static int parse_assignment(const uint8_t *line, uint32_t length,
                            uint32_t index, const char *name,
                            uint32_t *error_column) {
    skip_space(line, length, &index);
    if (index >= length || line[index] != '=') {
        *error_column = index + 1;
        return PARSE_INVALID_STATEMENT;
    }
    int variable = find_variable(name);
    if (variable < 0) {
        *error_column = 1;
        return PARSE_UNKNOWN_VARIABLE;
    }
    uint32_t assignment_column = index + 1;
    index++;
    value_t value;
    int status = parse_expression(line, length, &index, &value, error_column);
    if (status != 0)
        return status;
    status = statement_end(line, length, index, error_column);
    if (status != 0)
        return status;
    if (value.type != variables[variable].value.type) {
        *error_column = assignment_column;
        return PARSE_ASSIGNMENT_TYPE_MISMATCH;
    }
    variables[variable].value = value;
    return 1;
}

static int parse_line(const uint8_t *line, uint32_t length,
                      uint32_t line_offset, uint32_t line_number,
                      uint32_t next_line_offset, uint32_t next_line_number,
                      script_block_t blocks[], uint32_t *block_depth,
                      int *active, uint32_t *jump_offset,
                      uint32_t *error_line, uint32_t *error_column) {
    uint32_t index = 0;
    skip_space(line, length, &index);
    if (index == length || line[index] == '#')
        return 0;

    char keyword[SCRIPT_NAME_SIZE];
    if (parse_name(line, length, &index, keyword, error_column) != 0)
        return *active ? PARSE_INVALID_STATEMENT : 0;
    uint32_t keyword_end = index;
    skip_space(line, length, &index);

    if (strcmp(keyword, "if") == 0) {
        if (*block_depth >= SCRIPT_MAX_BLOCK_DEPTH) {
            *error_column = 1;
            return PARSE_BLOCK_TOO_DEEP;
        }
        script_block_t *block = &blocks[*block_depth];
        block->type = BLOCK_IF;
        block->parent_active = *active;
        block->has_else = 0;
        block->condition = 0;
        if (block->parent_active) {
            int status = parse_condition(line, length, index,
                                         &block->condition, error_column);
            if (status != 0)
                return status;
        }
        (*block_depth)++;
        *active = block->parent_active && block->condition;
        return 0;
    }

    if (strcmp(keyword, "while") == 0) {
        if (*block_depth >= SCRIPT_MAX_BLOCK_DEPTH) {
            *error_column = 1;
            return PARSE_BLOCK_TOO_DEEP;
        }
        script_block_t *block = &blocks[*block_depth];
        block->type = BLOCK_WHILE;
        block->parent_active = *active;
        block->has_else = 0;
        block->condition = 0;
        block->condition_offset = line_offset + index;
        block->condition_length = length - index;
        block->body_offset = next_line_offset;
        block->body_line_number = next_line_number;
        block->while_line_number = line_number;
        if (block->parent_active) {
            int status = parse_condition(line, length, index,
                                         &block->condition, error_column);
            if (status != 0)
                return status;
        }
        (*block_depth)++;
        *active = block->parent_active && block->condition;
        return 0;
    }

    if (strcmp(keyword, "else") == 0) {
        if (!*block_depth) {
            *error_column = 1;
            return PARSE_INVALID_BLOCK;
        }
        int status = statement_end(line, length, index, error_column);
        if (status != 0)
            return status;
        script_block_t *block = &blocks[*block_depth - 1];
        if (block->type != BLOCK_IF || block->has_else) {
            *error_column = 1;
            return PARSE_INVALID_BLOCK;
        }
        block->has_else = 1;
        *active = block->parent_active && !block->condition;
        return 0;
    }

    if (strcmp(keyword, "end") == 0) {
        if (!*block_depth) {
            *error_column = 1;
            return PARSE_INVALID_BLOCK;
        }
        int status = statement_end(line, length, index, error_column);
        if (status != 0)
            return status;
        script_block_t *block = &blocks[*block_depth - 1];
        if (block->type == BLOCK_WHILE && block->parent_active) {
            int condition;
            status = parse_condition(script_buffer + block->condition_offset,
                                     block->condition_length, 0, &condition,
                                     error_column);
            if (status != 0) {
                *error_line = block->while_line_number;
                return status;
            }
            if (condition) {
                block->condition = 1;
                *active = 1;
                *jump_offset = block->body_offset;
                *error_line = block->body_line_number;
                return 0;
            }
        }
        (*block_depth)--;
        *active = block->parent_active;
        return 0;
    }

    if (!*active)
        return 0;

    if (strcmp(keyword, "print") == 0)
        return parse_print(line, length, index, error_column);
    if (strcmp(keyword, "let") == 0)
        return parse_let(line, length, index, error_column);
    return parse_assignment(line, length, keyword_end, keyword, error_column);
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
        case PARSE_INVALID_FORMAT: return "invalid formatted-string placeholder";
        case PARSE_OUTPUT_TOO_LONG: return "formatted output exceeds 511 characters";
        case PARSE_NON_NUMERIC: return "arithmetic requires numeric values";
        case PARSE_DIVISION_BY_ZERO: return "division by zero";
        case PARSE_ARITHMETIC_OVERFLOW: return "arithmetic overflow";
        case PARSE_ASSIGNMENT_TYPE_MISMATCH: return "assignment type does not match variable";
        case PARSE_RESERVED_NAME: return "name is reserved by the language";
        case PARSE_EXPRESSION_TOO_DEEP: return "expression nesting limit reached";
        case PARSE_CONDITION_NOT_BOOL: return "condition must be a Bool or comparison";
        case PARSE_INVALID_COMPARISON: return "expected a comparison operator";
        case PARSE_COMPARISON_TYPE_MISMATCH: return "values cannot be compared with this operator";
        case PARSE_INVALID_BLOCK: return "unexpected or duplicate else/end";
        case PARSE_BLOCK_TOO_DEEP: return "conditional nesting limit reached";
        case PARSE_UNCLOSED_BLOCK: return "conditional block is missing end";
        case PARSE_EXECUTION_LIMIT: return "script execution limit reached";
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
    expression_depth = 0;

    script_block_t blocks[SCRIPT_MAX_BLOCK_DEPTH];
    uint32_t block_depth = 0;
    int active = 1;
    uint32_t line_number = 1;
    uint32_t line_start = 0;
    uint32_t executed_lines = 0;
    while (line_start < size) {
        uint32_t line_end = line_start;
        while (line_end < size && script_buffer[line_end] != '\n')
            line_end++;

        if (executed_lines >= SCRIPT_MAX_EXECUTED_LINES) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "run: %s:%u:1: %s\n",
                              filename, line_number,
                              parse_error_message(PARSE_EXECUTION_LIMIT));
            return -1;
        }
        executed_lines++;

        uint32_t next_line_start = line_end + (line_end < size ? 1 : 0);
        uint32_t next_line_number = line_number + 1;
        uint32_t jump_offset = SCRIPT_NO_JUMP;
        uint32_t error_line = line_number;
        uint32_t error_column = 0;
        int status = parse_line(script_buffer + line_start,
                                line_end - line_start, line_start, line_number,
                                next_line_start, next_line_number, blocks,
                                &block_depth, &active, &jump_offset,
                                &error_line, &error_column);
        if (status < 0) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "run: %s:%u:%u: %s\n",
                              filename, error_line, error_column,
                              parse_error_message(status));
            return -1;
        }

        if (jump_offset != SCRIPT_NO_JUMP) {
            line_start = jump_offset;
            line_number = error_line;
        } else {
            line_start = next_line_start;
            line_number = next_line_number;
        }
    }
    if (block_depth) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "run: %s:%u:1: %s\n",
                          filename, line_number,
                          parse_error_message(PARSE_UNCLOSED_BLOCK));
        return -1;
    }
    return 0;
}
