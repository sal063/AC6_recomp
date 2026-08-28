// Texture-swap cvar definitions.
//
// These live apart from ac6_texture_overrides.cpp because that translation unit
// needs <d3d12.h>/<dxgiformat.h> for the DXGI_FORMAT and D3D12_RESOURCE_DIMENSION
// enums its DDS layout math is written against, so it is only built on Windows.
// The cvars themselves are part of the user-facing config surface (they appear in
// ac6recomp.toml and the F4 overlay) and are read from main.cpp and the status
// overlay, so they must be defined on every platform.
//
// Once texture swapping is wired into the Vulkan texture cache these can move
// back, along with a portable DXGI_FORMAT enum shim.

#include <string>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(ac6_texture_swaps_enabled, false, "AC6/TextureSwaps",
                    "Enable AC6 texture dump and replacement support (default off: the "
                    "replacement lookup currently checks the filesystem on every texture "
                    "upload, which can cause frame stutters; enable for texture modding)");
REXCVAR_DEFINE_BOOL(ac6_texture_swaps_dump_enabled, false, "AC6/TextureSwaps",
                    "Dump host-ready textures to the user-data texture dump folder");
REXCVAR_DEFINE_BOOL(ac6_texture_swaps_replace_enabled, false, "AC6/TextureSwaps",
                    "Load matching replacement DDS files from the user-data texture override folders");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_dump_dir, "texture_dumps", "AC6/TextureSwaps",
                      "User-data subdirectory that stores dumped texture DDS files and metadata");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_override_dir, "override/textures", "AC6/TextureSwaps",
                      "User-data subdirectory that stores loose replacement texture DDS files");
REXCVAR_DEFINE_STRING(ac6_texture_swaps_mods_dir, "mods", "AC6/TextureSwaps",
                      "User-data subdirectory containing mod folders with texture overrides");
