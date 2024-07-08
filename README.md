# About
`ashe` is a simple **unix** single threaded (a)synchronous(she)ll written in C (not POSIX compliant).

It supports job control, all of the redirections, pipelines, conditionals, variable
expansion and other basic stuff you would find in `bash` or `fish-shell`.

Shell can recognize terminal lines allowing you to navigate input across terminal
lines rather than moving multiple lines in case input wraps across multiple
terminal lines.
This means `ashe` only works with terminals that wrap text.

It uses no dependencies (such as `curses`/`ncurses`) and is written to be compatible
with `C99`.

That being said this shell was made primarily for educational purposes and thus it lacks
in various features and many other functionalities you would expect from the more popular
shells.

## Build/Install
Before building/installing you can configure the build parameters in
`config.mk` and you can configure shell parameters in `src/aconf.h`.

Building from source using [GNU make](https://www.gnu.org/software/make/):
```sh
git clone https://github.com/b-jure/ashe 
cd ashe
make
```
Installing:
```sh
make clean && sudo make install
```

## Default Keybinds
List of default keybinds and actions:
- `Backspace`, `Delete` - delete character
- `End`, `Ctrl + e`     - move to end of the terminal row
- `Home`, `Ctrl + s`    - move to start of the terminal row
- `Left`, `Ctrl + h`    - move cursor to the left
- `Down`, `Ctrl + j`    - move cursor down one terminal row
- `Up`, `Ctrl + k`      - move cursor up one terminal row
- `Right`, `Ctrl + l`   - move cursor to the right
- `Ctrl + n`            - traverse history forwards (next)
- `Ctrl + p`            - traverse history backwards (previous)
- `Ctrl + r`            - clear screen (keeps scroll-back)
- `Ctrl + w`            - delete text behind the cursor
- `Ctrl + d`            - delete text in front of the cursor
- `Ctrl + x`            - exits the shell (full cleanup)

**Note:** in case redrawing bugg occurs just clear the screen (`Ctrl+r`),
if that doesn't fix it just send SIGINT (`Ctrl+c`).


## Shell syntax
List of all syntax symbols and their meaning.
Optional parameters are enclosed in `[]`.

- `c1 && c2` - conditional AND; list of sequences of one or more pipelines, the right command
`c2` is executed only if `c1` returns exit status of zero (`success`).

- `c1 || c2` - conditional OR; list of sequences of one or more pipelines, the right command
`c2` is executed only if `c1` returns non-zero exit status.

- `[ln]<&[n]` - duplicates descriptor open for reading; descriptor gets opened (duplicated) on
the number provided on the left side of the expression `ln`, if descriptor number `n` is not
provided on the right side of the expression descriptor number `1` will be used. 
This can also close descriptor number `ln` if `-` is provided on the right side of the expression
instead of the descriptor number `n`. 

- `[ln]>&[n, filepath]` - duplicates descriptor open for writing; descriptor gets opened (duplicated)
on the number provided on the left side of the expression `ln`, if descriptor number `n` is
not provided on the right side of the expression descriptor number `1` will be used.
This also closes descriptor `ln` if `-` is provided on the right side of the expression
instead of the descriptor number `n`. 
Additionally if both `ln` and `n` are absent then the behaviour is identical to
`&>` and the file provided by the `filepath` will be used instead.

- `>>filepath` - redirects `stdout` **appending** it into the file provided by the `filepath` on
the right side of the expression.

- `&>filepath` - redirects `stdout` and `stderr` into the file provided by the `filepath` on
the right side of the expression.

- `&>>filepath` - redirects `stdout` and `stderr` **appending** it into the file provided by
the `filepath` the right side of the expression.

- `[n]<>filepath` - opens the file provided by the `filepath` on the right side of the expression
both for reading and writing on file descriptor on the left side of the expression. If
`n` is not specified descriptor `0` is used instead.

- `[n]<filepath` - opens the file provided by the `filepath` to be opened for reading
on file descriptor `n`, in case `n` is not provided, descriptor number `0` is used.

- `[n]>filepath` - opens the file provided by the `filepath` to be opened for writing
on file descriptor `n`, in case `n` is not provided, descriptor number `1` is used.

- `;` - separates pipeline lists. Generally used as a separator or can be used as indicator
that the command is being run in the foreground.

- `cmd (cmd)` - command reordering; command in `()` gets executed first and it's output is
piped into the input of `cmd` that is before it. Identical to the pipeline `|`.

- `|` - pipeline. Sequence of one or more commands separated by `|`. The output of each command
in the pipeline is connected via a `pipe(2)` to the input of the next command.

- `c1&` - runs command `c1` in the background (asynchronously) so the shell won't wait for the
command to be finished executing.

- `$var_name` - expands environmental variables in this example the `var_name`. The only way to
prevent this expansion would be to escape the `$` like this `\$`.

- `""` - quotes are used in order to write multi-line text and escape reserved symbols shell uses.
**Note** that `$` will still expand variables inside of quotes.

- `>|` - noclobber. NOT IMPLEMENTED but mentioned in the source files!


## Builtin commands
List of builtin commands:
- `cd` - change current directory.
- `pwd` - print working directory.
- `clear` - clear screen (and scrollback).
- `builtin` - list builtin commands.
- `fg` - move job/s into foreground.
- `bg` - move job/s into background.
- `jobs` - list jobs, their metadata and their status.
- `exec` - open/close/copy file descriptors and/or execute a command.
- `exit` - exit the shell.
- `penv` - print environmental variable/s.
- `senv` - set environmental variable.
- `renv` - remove environmental variable.


## Configuration
Configuration is done inside of `aconf.h` header file in `src` directory before
compiling/building the shell.
