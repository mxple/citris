# Citris

A modern Tetris training tool aiming to be the best tool of its kind out there.

**This project is under development.** More features are on the way.

<img width="876" height="873" alt="image" src="https://github.com/user-attachments/assets/95ea8843-55fb-4c14-a4fb-0f235865632f" />

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

