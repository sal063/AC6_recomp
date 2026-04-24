# AC6 Asset Pipeline

This pipeline extracts and converts the AC6 assets we currently understand.

It automates these stages:

1. `DATA.TBL` + `DATA00.PAC` / `DATA01.PAC` index extraction
2. Runtime PAC decode dump parsing
3. Recursive `FHM` extraction
4. `SWG` UI metadata parsing
5. `NTXR` texture export

## Important Limitation

The pipeline can process **all PAC entries and all runtime dumps you already have**, but it **cannot yet offline-decompress every compressed PAC entry in the archive** by itself.

That means:

- Raw PAC entries are handled in one pass.
- Runtime-decoded assets are handled in one pass.
- If a compressed asset has never been decoded by the game and never appeared in `out/ac6_pac_runtime_dump`, this pipeline cannot currently materialize it.

So the pipeline is "all at once" for the **current corpus**, not yet "decode the entire PAC archive from scratch with no runtime help".

If you want truly complete one-shot extraction of every compressed PAC asset without launching the game, the remaining missing piece is an offline implementation of AC6's mode-1 decompressor.

## Prerequisites

- The game assets exist at:
  - `C:\ext\New folder\AC6_recomp\out\build\win-amd64-relwithdebinfo\assets`
- If you want runtime-decoded content included, you must already have:
  - `C:\ext\New folder\AC6_recomp\out\ac6_pac_runtime_dump`

To collect runtime dumps in future runs:

```powershell
$env:AC6_DUMP_PAC_DECODED='1'
& 'C:\ext\New folder\AC6_recomp\out\build\win-amd64-relwithdebinfo\ac6recomp.exe'
```

Or use the launcher helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\launch_ac6_with_pac_dump.ps1
```

That script sets `AC6_DUMP_PAC_DECODED=1` and launches `ac6recomp.exe` for you.

## One-Command Usage

From the repo root:

```powershell
python .\tools\run_ac6_asset_pipeline.py
```

This uses the default paths:

- Asset root: `out\build\win-amd64-relwithdebinfo\assets`
- Raw PAC output: `out\ac6_pac_extracted_raw`
- Runtime dump input: `out\ac6_pac_runtime_dump`
- Typed FHM output: `out\ac6_runtime_fhm_typed`
- SWG output: `out\ac6_runtime_swg_parsed`
- Texture output: `out\ac6_runtime_ntxr_exported`

The wrapper prints a final JSON summary with the current corpus totals, including:

- PAC entries extracted
- runtime `FHM` container count
- parsed `SWG` files
- exported/skipped `NTXR` textures

## Useful Variants

Use a custom asset root:

```powershell
python .\tools\run_ac6_asset_pipeline.py --asset-root 'C:\path\to\assets'
```

Skip PAC re-extraction and only process existing dumps:

```powershell
python .\tools\run_ac6_asset_pipeline.py --skip-pac-extract
```

Recommended workflow after a new play session:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\launch_ac6_with_pac_dump.ps1
python .\tools\run_ac6_asset_pipeline.py --skip-pac-extract
```

Extract only PAC entries marked raw:

```powershell
python .\tools\run_ac6_asset_pipeline.py --raw-only
```

## Output Folders

- Raw PAC extraction:
  - `C:\ext\New folder\AC6_recomp\out\ac6_pac_extracted_raw`
- Parsed runtime FHM corpus:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_fhm_typed`
- Parsed SWG metadata:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_swg_parsed`
- Exported textures:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_ntxr_exported`

## What To Open

- Main texture manifest:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_ntxr_exported\manifest.json`
- SWG manifest:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_swg_parsed\manifest.json`
- Example exported textures:
  - `C:\ext\New folder\AC6_recomp\out\ac6_runtime_ntxr_exported`

## Notes About Texture Types

Not every texture is a normal color image.

The exporter now handles:

- Standard tiled `R8` atlases
- Standard tiled `RGBA8` atlases
- `BC1` textures
- `BC3` textures
- Packed-mip `BC3` families
- Six-face `BC1` assets using contact-sheet previews
- Several support textures stored as grayscale-in-ARGB

Some outputs are still valid but are things like:

- masks
- glyph sheets
- lookup textures
- normal maps
- cubemap-style faces

## Current Status

At the time this guide was written, the texture exporter was producing:

- `845` exported textures
- `0` skipped textures

That count applies to the currently available runtime dump corpus.
