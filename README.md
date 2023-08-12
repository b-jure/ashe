# About
`ashe` is a simple **unix** single threaded async shell written in C.

It supports job control, all of the redirections, pipelines, conditionals, variable
expansion and other basic stuff you would find in `bash` or `fish-shell`.
The only "unique" feature that ashe does is ease of multiline input editing 
and navigation.
It provides most basic input controls but the way it is written enables easy
implementation of all kinds of motions, keybinds for text navigation and in general
terminal navigation/drawing. It also doesn't rely on `curses`/`ncurses` library, acually
it doesn't rely on anything outside the `glibc` (`C11`).

Each line is represented as its own seperate row this includes the **wrapped terminal lines**.
What this enables is text-editor-like movement inside the shell. Now why is this even
important, shouldn't the user just create a shell script for longer commandlines? Yes that
is a good point but let's say you just want to run a big commandline once, or want it to
be saved in the recent commands history, then the fast and easy text editing and movement
in shell really helps. Personally I think having ergonomic cursor movement inside
a shell is a simple thing to do with no clear negative tradeoffs.

That being said this shell was made primarily for educational purposes and thus it lacks
the real interpreter outside the shell's commandline to even create `ashe` scripts. This
means `ashe` lacks its own _language_ therefore it doesn't support/have `.ashe` scripts, of
course this is a thing that I aim implementing down the road. And there are a lot of
other things that need implementing (regex, history, vim-motions, sytnax highlighting, 
autocompletion, system-wide clipboard manipulation....).

## Build/Install
Building from source using [cmake](https://cmake.org/):
```sh
git clone https://github.com/SigmaBale/ashe.git
cd ashe
mkdir build
cd build
cmake ..
make
```

while in `build` directory to install run (_default installation path_ -> `/usr/local/bin`):
```sh
sudo cmake --install .
```

### AUR/Arch/Manjaro
If you are on arch-based distribution there is `AUR` package that I am maintaining, install using `pamac`:
```
pamac install ashe
```
or with `yay`:
```
yay -S ashe
```
