# Aegis Scripting Language

Aegis is a small interpreted language built into AegisOS. Scripts are saved as
ordinary text files (often with the `.aeg` extension) in AegisFS and run with
the shell's `run` command. The language supports variables, arithmetic,
printing, comments, conditional `if`/`else` blocks, and `while` loops.

## Quick start

Create a script in the editor:

```text
edit hello.aeg
```

Enter the following, then press Ctrl+S to save and Ctrl+Q to return to the
shell:

```aegis
# My first Aegis program
print "Hello from Aegis!"
print "I am running inside AegisOS."
let version = 1
print version
```

Run it:

```text
chmod 755 hello.aeg
run hello.aeg
```

The script prints:

```text
Hello from Aegis!
I am running inside AegisOS.
1
```

## Syntax

Each non-empty line contains one statement. Leading and trailing spaces are
ignored.

### Variables

Declare a variable with `let`, a name, and an expression. The expression's
result type is inferred:

```aegis
let name = "Aegis"
let year = 2026
let ratio = 1.250
let ready = true
let initial = 'A'
```

You can also state the type explicitly:

```aegis
let name: String = "Aegis"
let year: Int = 2026
let ratio: Float = 1.250
let ready: Bool = true
let initial: Char = 'A'
```

The supported types are `String`, `Int`, `Float`, `Bool`, and `Char`.
Type names are case-sensitive. An explicit type must match the expression's
result type.
Variable names begin with a letter or underscore and may then contain letters,
digits, and underscores. A variable can only be declared once in a script.
The names `let`, `print`, `true`, `false`, `if`, `else`, `while`, and `end`
are reserved. Variables exist only for the duration of one `run` command.

`Int` values are signed 32-bit integers. `Float` values are stored as
fixed-point numbers with up to three decimal places; they are not hardware
floating-point values. A `Char` is exactly one character in single quotes.
Strings and variable names are limited to 127 and 31 characters respectively.

### Arithmetic and reassignment

Numeric expressions support integer and fixed-point arithmetic, unary `+` and
`-`, and parentheses:

```aegis
let width = 8
let height = 3
let area = width * height
let half = area / 2.0
let adjusted = (area + 4) * -2
print "Area: {area}; half: {half}; adjusted: {adjusted}"
```

Operators follow conventional precedence: unary signs first, then `*` and `/`,
then `+` and `-`; operators at the same level are evaluated left to right.
Integer-only arithmetic produces an `Int`, and integer division truncates
toward zero. If either operand is a `Float`, the operation produces a `Float`;
integer operands are promoted to fixed-point. Float results are truncated
toward zero to three fractional digits. Parentheses may be nested up to 32
levels. Division by zero and arithmetic overflow stop the script with an error.

Assign a new expression to an existing variable with `name = expression`.
Reassignment keeps the variable's original type:

```aegis
let count = 1
count = count + 1
print count
```

The assigned expression must have the same type as the variable. Variables
must be declared before they are used or reassigned.

### Conditions

Use `if`, optional `else`, and `end` on separate lines to conditionally run
statements:

```aegis
let score = 12
if score >= 10
    print "You passed"
else
    print "Try again"
end
```

Conditions may be a `Bool` variable or literal, or a comparison using `==`,
`!=`, `<`, `<=`, `>`, or `>=`. Comparisons work with numeric values; `Int` and
`Float` values can be compared with each other. Strings support `==` and `!=`,
and `Bool` values support `==` and `!=`. `Char` values support all six
comparisons. Other type combinations and unsupported operators report an
error. A conditional block may contain another conditional block; nesting is
limited to 32 levels. Each `if` must end with `end`, and may have at most one
`else`. Statements in the unselected branch are skipped.

### `while` loops

Use `while` with a condition and close the loop with `end`. The condition is
checked before each iteration:

```aegis
let count = 1
while count <= 5
    print count
    count = count + 1
end
```

Loops can be nested with other loops and conditional blocks, up to the same
32-level block-nesting limit. Each `while` must have a matching `end`.
Variables are script-scoped rather than block-scoped, so declare a loop
variable once before the loop and update it inside the loop.
Execution is limited to 10,000 processed script lines per `run` command,
counting blank lines, comments, skipped branches, and lines processed again
by loops. If the limit is reached, the script stops with an error; this also
prevents a non-terminating loop from hanging the system.

### `print`

`print` displays a double-quoted string or a previously declared variable,
followed by a newline. In a quoted string, `{name}` is replaced by the value
of the variable `name`:

```aegis
print "Hello"
print name
print ratio
print "Hello, {name}! Version {year}."
```

Formatted placeholders accept any declared type. The braces must contain a
single variable name; expressions and format specifiers are not supported.
Use `{{` and `}}` to display literal braces:

```aegis
print "{{name}} is a placeholder example"
```

Formatted output is limited to 511 characters per `print` statement.
Placeholders in string variable declarations are not expanded; interpolation
currently applies to quoted strings passed directly to `print`.

Supported escapes inside strings:

| Escape | Meaning |
|---|---|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\t` | Tab |
| `\'` | Single quote |

For example:

```aegis
print "She said: \"hello\""
print "first line\nsecond line"
print 'A'
```

### Comments

A `#` begins a comment outside a string. A line whose first non-space character
is `#` is ignored; comments may also follow a statement:

```aegis
# This line is ignored
print "This runs" # explanation
```

Blank lines are ignored.

## Errors and limits

When a line is invalid, `run` reports the script filename, line, and column,
then stops. The script is read from AegisFS and is limited to the filesystem's
maximum file size of 32 KiB.

User-defined functions are not supported yet.
