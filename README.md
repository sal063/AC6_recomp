DEV TESTING BRANCH. THINGS WILL BREAK HERE

# AC6Recomp

> [!CAUTION]
> This project is still work in progress. It can boot and run in-game, but bugs, crashes, and missing functionality should be expected.

A native PC port of **Ace Combat 6: Fires of Liberation** (Xbox 360), built on top of the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The Xbox 360 PowerPC binary is statically recompiled to x86-64 so the original game logic runs natively on your host CPU, with a fully native D3D12/Vulkan renderer replacing the original Xenos GPU pipeline.

## Repository policy

This repository contains source code only.

Do **not** commit or redistribute:

- retail game data
- `default.xex`
- disc images, packages, title updates, or firmware files
- console keys or any other proprietary Microsoft / publisher material

Users must supply their own legally obtained game files locally.

---

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| [CMake](https://cmake.org/) | 3.25+ | |
| [Ninja](https://ninja-build.org/) | any recent | required generator |
| [Clang/LLVM](https://releases.llvm.org/) | any recent | `clang` / `clang++` must be on `PATH` |
| Windows SDK | 10.0.19041+ | D3D12 headers (Windows only) |

> [!NOTE]
> The Linux preset uses `clang-20` / `clang++-20` directly. Install the versioned binaries via your distro's package manager (`apt install clang-20`) or via the [LLVM APT repository](https://apt.llvm.org).

---

## Acquiring the game files

1. Obtain the original Xbox 360 disc image (ISO) by dumping your own disc.  
   Guides and tools: [consolemods.org – ISO Extraction & Repacking](https://consolemods.org/wiki/Xbox:ISO_Extraction_%26_Repacking)
2. Extract the XEX and game data from the ISO.
3. Place the resulting files inside the `assets/` directory (created manually — it is git-ignored):

```text
assets/
  default.xex        ← required by the codegen step
  media/             ← game data (audio, video, maps, …)
  …
```

---

## Clone

```bash
git clone https://github.com/sal063/AC6_recomp.git
cd AC6_recomp
```

> [!NOTE]
> The ReXGlue SDK (`thirdparty/rexglue-sdk/`) is vendored directly in the repository. No submodule init is needed.

---

## Build

### 1 — Configure

```bash
cmake --preset win-amd64-relwithdebinfo
```

### 2 — Generate the recompiled code (first time, and after updating `default.xex`)

```bash
cmake --build --preset win-amd64-relwithdebinfo --target ac6recomp_codegen
```

This step reads `assets/default.xex`, lifts all PowerPC instructions to C++, and writes the output to `generated/`. It can take a few minutes.

### 3 — Re-run CMake configure

```bash
cmake --preset win-amd64-relwithdebinfo
```

Re-run configure after codegen so CMake picks up the generated `generated/sources.cmake` file and adds the generated `.cpp` sources to the target.

### 4 — Build the runtime

```bash
cmake --build --preset win-amd64-relwithdebinfo
```

The executable is placed at:

```
out/build/win-amd64-relwithdebinfo/ac6recomp.exe
```

> [!TIP]
> `RelWithDebInfo` is the recommended preset — it gives near-release performance with symbols intact for debugging. A full `Release` build disables assertions and can be used for distribution.

### Available presets

| Preset | Platform | Build type |
|---|---|---|
| `win-amd64-debug` | Windows | Debug |
| `win-amd64-release` | Windows | Release |
| `win-amd64-relwithdebinfo` | Windows | RelWithDebInfo ✅ recommended |
| `linux-amd64-debug` | Linux | Debug |
| `linux-amd64-release` | Linux | Release |
| `linux-amd64-relwithdebinfo` | Linux | RelWithDebInfo |

---

## Run

```bash
./out/build/win-amd64-relwithdebinfo/ac6recomp assets
```

The single argument is the path to the directory containing your game files (`assets/` by default). The runtime resolves all paths relative to it.

---

## Linux

Substitute `win-amd64-relwithdebinfo` with `linux-amd64-relwithdebinfo` in every command above.

```bash
cmake --preset linux-amd64-relwithdebinfo
cmake --build --preset linux-amd64-relwithdebinfo --target ac6recomp_codegen
cmake --preset linux-amd64-relwithdebinfo
cmake --build --preset linux-amd64-relwithdebinfo
./out/build/linux-amd64-relwithdebinfo/ac6recomp assets
```

---

## Project layout

```
AC6_recomp/
├── src/                        Host-side runtime & renderer
│   ├── main.cpp
│   ├── ac6_native_graphics.*   Xenon → native GPU command translation
│   ├── ac6_native_renderer/    Native rendering backend (D3D12 / Vulkan)
│   │   ├── backends/           Per-API backend implementations
│   │   ├── frame_plan.*        Frame dependency graph construction
│   │   ├── frame_scheduler.*   CPU/GPU timeline management
│   │   ├── native_renderer.*   Top-level renderer orchestration
│   │   └── render_device.*     Device abstraction layer
│   └── d3d_hooks.*             Low-level D3D intercept layer
├── thirdparty/rexglue-sdk/     ReXGlue SDK (vendored)
├── assets/                     ← NOT in repo; place your game files here
├── generated/                  ← NOT in repo; output of codegen step
├── CMakeLists.txt
└── CMakePresets.json
```

---

## License

See [LICENSE](LICENSE).
