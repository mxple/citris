# Citris

A modern Tetris training tool aiming to be the best tool of its kind out there.

Try it online [here!](https://mxple.wtf/citris)

Native builds for all 3 major OSs are also available in Releases.

**This project is under development.** Check back for more features.

## Planned Features

Training modes for T-spin setups, PC, and downstack practice.

AI assisted mode for PC practice and midgame tactics.

VS mode against AI or local network.

## Contributing

Contributing skins, bug reports, suggestions, and small features is much appreciated! Open a PR/issue to get started.

## Building

You'll need a C++23 compiler, [SDL](https://www.libsdl.org/), and [ImGUI](https://github.com/ocornut/imgui).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/tetris
```

Settings are loaded from `settings.ini` in the project root. Copy `assets/default_settings.ini` to get started, or the game will use sensible defaults.

