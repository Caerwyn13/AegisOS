# Aegis Scripting Language

Aegis is a small interpreted language built into AegisOS. Scripts are saved as
ordinary text files (often with the `.aeg` extension) in AegisFS and run with
the shell's `run` command. The language supports variables, printing, blank
lines, and comments.

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

Declare a variable with `let`, a name, and a value. The value's type is
inferred:

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
Type names are case-sensitive. An explicit type must match the literal's type.
Variable names begin with a letter or underscore and may then contain letters,
digits, and underscores. A variable can only be declared once in a script.
Variables exist only for the duration of one `run` command.

`Int` values are signed 32-bit integers. `Float` values are stored as
fixed-point numbers with up to three decimal places; they are not hardware
floating-point values. A `Char` is exactly one character in single quotes.
Strings and variable names are limited to 127 and 31 characters respectively.

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

This initial version does not support expressions, reassignment, conditions,
loops, or user-defined functions. Values assigned to variables must be
supported literals; variables can be printed but not yet used in expressions.
