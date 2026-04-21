# Texture Swaps

The texture swap pipeline now lives in the authoritative RexGlue/Xenia D3D12 texture cache. It is dump-and-replace based, so you do not need to hand-author per-texture IDs before you can start modding.

## Workflow

1. Launch the game and let the texture cache see the textures you care about.
2. Dumped files appear under:
   - `%USERPROFILE%\\Documents\\ac6recomp\\texture_dumps\\`
   - or the directory set by the `user_data_root` runtime CVAR.
3. Each dumped texture produces:
   - `<stable_key>.dds`
   - `<stable_key>.json`
4. Edit the dumped DDS without changing its format, dimensions, array size, or mip count.
5. Place the replacement DDS at:
   - `override/textures/<stable_key>.dds`
   - or `mods/<mod_name>/textures/<stable_key>.dds`
6. Restart or cause the texture to reload.

`override/textures` wins over mod folders. Within `mods`, lexicographically later folder names win.

## Stable Keys

Dump filenames are generated from the texture cache key, not guessed game names. The filename includes:

- a hash of the full cache key
- base and mip pages
- dimension
- size
- mip count
- guest format
- endian/tiled/packed/signed/scaled flags

That makes the key stable enough for round-tripping replacements while still being readable.

## Metadata Sidecars

Each JSON sidecar records:

- guest texture key fields
- chosen host DXGI format
- AC6 frame index
- latest AC6 backend signature ID
- active VS/PS hashes
- signature tags from the AC6 backend classifier

This is meant for filtering and later tooling, not for the core replacement path.

## Current Scope

First pass limitations:

- replacement files must be DX10-header DDS files
- replacement format, dimensions, depth/array size, and mip count must match exactly
- cube textures are skipped
- unsupported DXGI formats fall back to the original guest texture

The fallback path is always the original guest texture load. A bad or missing replacement will not block rendering.
