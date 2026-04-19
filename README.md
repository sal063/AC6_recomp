DEV TESTING BRANCH. THINGS WILL BREAK HERE

# AC6Recomp

> [!CAUTION]
> This project is still work in progress. It can boot and run in-game, but bugs, crashes, and missing functionality should be expected.

A native PC port of **Ace Combat 6: Fires of Liberation** built on top of the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The Xbox 360 PowerPC game code is statically recompiled to x86-64, while visible rendering currently remains authoritative in the vendored RexGlue/Xenia graphics backend.

The AC6-specific graphics layer in this repo is now focused on:

- frame capture and diagnostics
- swap-path inspection and overlay reporting
- backend-fix routing for AC6-specific rendering issues
- future selective override and modding hooks

The legacy AC6 replay renderer is still present as experimental tooling, but it is **not** the default render path and it does **not** hijack presentation unless `ac6_graphics_mode=legacy_replay_experimental` and `ac6_experimental_replay_present=true`.

## Repository policy

This repository contains source code only.

Do **not** commit or redistribute:

- retail game data
- `default.xex`
- disc images, packages, title updates, or firmware files
- console keys or any other proprietary Microsoft / publisher material

Users must supply their own legally obtained game files locally.

## Build

```bash
cmake --preset win-amd64-relwithdebinfo
cmake --build --preset win-amd64-relwithdebinfo --target ac6recomp_codegen
cmake --preset win-amd64-relwithdebinfo
cmake --build --preset win-amd64-relwithdebinfo
```

The executable is placed at `out/build/win-amd64-relwithdebinfo/ac6recomp.exe`.

## Runtime Defaults

The default AC6 graphics configuration after this pivot is:

- `ac6_native_graphics_enabled=true`
- `ac6_graphics_mode=hybrid_backend_fixes`
- `ac6_render_capture=true`
- `ac6_experimental_replay_present=false`

That means the RexGlue/Xenia D3D12 backend remains the visible renderer by default, while AC6-specific analysis and diagnostics stay active.

## Project layout

```text
AC6_recomp/
|- src/
|  |- ac6_backend_fixes/       AC6-specific backend diagnostics and fix routing
|  |- ac6_native_graphics.*    AC6 frame-boundary analysis and overlay status
|  |- ac6_native_renderer/     Experimental replay renderer and research tooling
|  |- d3d_hooks.*              Guest D3D capture and shadow-state hooks
|  `- render_hooks.*           Timing and frame pacing hooks
|- thirdparty/rexglue-sdk/     Vendored RexGlue SDK
|- generated/                  Generated recomp sources
|- assets/                     Local game files, not kept in repo
`- docs/RENDERER_ARCHITECTURE.txt
```

## License

See [LICENSE](LICENSE).
