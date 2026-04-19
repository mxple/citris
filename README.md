# Citris

A modern Tetris training tool aiming to be the best tool of its kind out there.

Try it online [here!](https://mxple.wtf/citris)

Native builds for all Windows, MacOS, and Linux are available in Releases.

**This project is under development.**

## Features

- [x] 40L, Blitz, Cheese race, and Freeplay
- [x] T-spin and advanced T-spin practice
- [x] Downstack AI
- [x] PC finder
- [ ] Custom queue + maps
- [ ] Opener library + trainer
- [ ] Finesse and advanced finesse trainer
- [ ] VS mode against AI
- [ ] Networked multiplayer (?)

## Contributing

Contributing skins, bug reports, suggestions, and code is much appreciated! Open a PR/issue to get started.

## Building

You'll need a C++23 compiler, [SDL](https://www.libsdl.org/), and [ImGUI](https://github.com/ocornut/imgui).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/tetris
```

Settings are loaded from `settings.ini` in the project root. Copy `assets/default_settings.ini` to get started, or the game will use sensible defaults.

## Acknowledgements

Portions of this codebase are either inspired by or direct ports of existing software.

T-spin practice modes are ported from [himitsuconfidential](https://github.com/himitsuconfidential/downstack-practice).

PC finder is ported from [MinusKelvin](https://github.com/MinusKelvin/pcf).

The following projects were used as references: [cold-clear](https://github.com/MinusKelvin/cold-clear), [fusion](https://github.com/MochBot/fusion), [misamino](https://github.com/misakamm/MisaMino), [clys](https://github.com/citrus610/clys), and [blockfish](https://github.com/blockfish/blockfish).
