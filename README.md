# Citris

A modern Tetris training tool aiming to be the best tool of its kind out there.

**This project is under development.** More features are on the way.

## Default controls

| Key | Action |
|-----|--------|
| Left / Right | Move |
| Up | Rotate clockwise |
| Z | Rotate counter-clockwise |
| A | Rotate 180 |
| Space | Hard drop |
| Down | Soft drop |
| C | Hold |
| U | Undo |

All controls can be rebound in `settings.ini`.

## Planned Features

Presets for Sprint, Blitz, and Cheese Race.

Training modes for T-spin setups, PC, and downstack practice.

AI assisted mode for PC practice and midgame tactics.

VS mode against AI or local network.

## Building

You'll need a C++23 compiler and [SFML 3](https://www.sfml-dev.org/).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/tetris
```

Settings are loaded from `settings.ini` in the project root. Copy `assets/default_settings.ini` to get started, or the game will use sensible defaults.

