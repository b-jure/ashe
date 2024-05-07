# About
`ashe` is a simple **unix** single threaded (a)synchronous(she)ll written in C (not POSIX compliant).

It supports job control, all of the redirections, pipelines, conditionals, variable
expansion and other basic stuff you would find in `bash` or `fish-shell`.

The weird thing that differs from all other shells would be that ashe can
recognize terminal lines allowing you to navigate input across terminal lines
rather than moving multiple lines in case input wraps across multiple terminal
lines.

This means `ashe` only works with terminals that wrap text.
It also doesn't rely on `curses`/`ncurses` library, actually it doesn't rely on anything
outside the `libc` (`C99`).

That being said this shell was made primarily for educational purposes and thus it lacks
the real interpreter and many other functionalities you would expect to have in
a shell program.

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
