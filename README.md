# AC6Recomp

> [!CAUTION]
> This project is still work in progress. It can boot and run in-game, but bugs, crashes, and missing functionality should be expected.

Recompiled using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

## Repository policy

This repository is intended to contain source code only.

Do not commit or redistribute:

- retail game data
- `default.xex`
- disc images, packages, title updates, or firmware files
- console keys or any other proprietary Microsoft / publisher material

Users must supply their own legally obtained game files locally. This repository does not include those files
## Prerequisites

- [CMake](https://cmake.org/) 3.25+
- [Ninja](https://ninja-build.org/)
- [Clang](https://releases.llvm.org/) (LLVM/Clang toolchain)
- A legally obtained copy of the game, prepared by the end user outside this repository

## Clone

Clone the repository with submodules:

```bash
git clone --recursive <your-repo-url>
cd AC6Recomp
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

## Local file layout

Place your personally obtained game files in `assets/` so they are available only on your machine and remain untracked by Git.

Expected minimum layout:

```text
assets/
  default.xex
  ...
```

The codegen config expects `assets/default.xex`.

## Build

Generate recompiled code:

```bash
cmake --preset win-amd64-relwithdebinfo
cmake --build --preset win-amd64-relwithdebinfo --target ac6recomp_codegen
```

Build the project:

```bash
cmake --build --preset win-amd64-relwithdebinfo
```

`RelWithDebInfo` is the recommended preset at the moment.

The executable will be produced in `out/build/win-amd64-relwithdebinfo/`.

## Run

```bash
./out/build/win-amd64-relwithdebinfo/ac6recomp assets
```

## Linux

Replace `win-amd64-relwithdebinfo` with `linux-amd64-relwithdebinfo` in the commands above.
