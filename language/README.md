# Aegis Scripting Language

Aegis is a small interpreted language built into AegisOS. Scripts are saved as
ordinary text files in AegisFS and run with the shell's `run` command. The first
version supports printing text, blank lines, and comments.

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
```

Run it:

```text
run hello.aeg
```

The script prints:

```text
Hello from Aegis!
I am running inside AegisOS.
```

## Syntax

Each non-empty line contains one statement. Leading and trailing spaces are
ignored.

### `print`

`print` displays a double-quoted string followed by a newline:

```aegis
print "Hello"
```

Supported escapes inside strings:

| Escape | Meaning |
|---|---|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\t` | Tab |

For example:

```aegis
print "She said: \"hello\""
print "first line\nsecond line"
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

This initial version has no variables, expressions, conditions, loops, or
user-defined functions yet.
