#!/usr/bin/env python3
"""Export AC6 audio to .xma (and optionally .wav via vgmstream-cli).

Two sources are handled:

1. Runtime FHM corpus (default).
   The FHM extractor drops audio entries as `*_RIFF.bin`. Each is a Project
   Aces RIFF/WAVE container with fmt (tag 0x0165 / XMA1), ALIG, x2st (XMA2
   stream descriptor), data. The RIFF size field is non-standard (= file_size
   instead of file_size - 8) and gets patched on export.

2. Monolithic asset packs (--packs).
   bgmpack.bin / voicepack_*.bin / demopack_*.bin are flat concatenations of
   independent RIFF/WAVE streams (XMA2 for BGM, XMA1 for voice/demo), aligned
   to 0x800 sectors. We walk the declared RIFF size to split them.

Note: in-game SFX (engines, weapons, explosions) are NOT in these packs --
they live in BSN/BFX/nusc banks pointing into DATA*.PAC entries that aren't
runtime-decoded yet. Those need either the offline mode-1 decompressor or
gameplay capture with AC6_DUMP_PAC_DECODED=1.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = REPO_ROOT / "out" / "ac6_runtime_fhm_typed"
DEFAULT_OUTPUT = REPO_ROOT / "out" / "ac6_runtime_audio"
DEFAULT_ASSETS = REPO_ROOT / "out" / "build" / "win-amd64-relwithdebinfo" / "assets"

PACK_FILES = [
    "bgmpack.bin",
    "voicepack_eng.bin",
    "voicepack_jpn.bin",
    "demopack_eng.bin",
    "demopack_jpn.bin",
]


def split_pack(pack_path: Path, out_dir: Path) -> int:
    """Walk a concatenated multi-RIFF pack and write each stream as its own .xma.

    Each stream begins with `RIFF`<u32 size>`WAVE`. The next stream starts at
    `pos + 8 + declared_size`, padded up to a 0x800-byte sector boundary.
    """
    SECTOR = 0x800
    written = 0
    blob = pack_path.read_bytes()
    end = len(blob)
    pos = 0
    stem = pack_path.stem
    while pos + 12 <= end:
        if blob[pos:pos + 4] != b"RIFF" or blob[pos + 8:pos + 12] != b"WAVE":
            # Skip to next sector and re-check; some packs have padding regions.
            pos = (pos + SECTOR) & ~(SECTOR - 1)
            continue
        declared = struct.unpack_from("<I", blob, pos + 4)[0]
        total = 8 + declared
        if pos + total > end:
            print(f"  [{stem}] truncated stream at 0x{pos:x} (declared {declared}, "
                  f"only {end - pos - 8} available); stopping")
            break
        out_name = f"{stem}__{written:05d}_off{pos:08x}.xma"
        (out_dir / out_name).write_bytes(blob[pos:pos + total])
        written += 1
        # Advance to next sector boundary.
        next_pos = (pos + total + SECTOR - 1) & ~(SECTOR - 1)
        if next_pos == pos:
            break
        pos = next_pos
    print(f"  [{stem}] {written} streams written")
    return written


def find_riffs(root: Path) -> list[Path]:
    return sorted(p for p in root.rglob("*RIFF.bin") if p.is_file())


def derive_output_name(riff: Path, root: Path) -> str:
    rel = riff.relative_to(root)
    parts = list(rel.parts)
    # parts[-1] is e.g. "001_RIFF.bin"; drop suffix.
    leaf = parts[-1].removesuffix(".bin")
    # Tag with idx_NNNN and any FHM container directories on the path so names stay unique.
    prefix_parts = [p.replace("_FHM_", "f") for p in parts[:-1]]
    prefix = "__".join(prefix_parts)
    return f"{prefix}__{leaf}.xma"


def patch_riff(blob: bytearray) -> tuple[bytearray, dict]:
    """Verify it's a RIFF/WAVE and fix the size field. Return (patched, info)."""
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE container")

    file_len = len(blob)
    declared = struct.unpack_from("<I", blob, 4)[0]
    fixed_size = file_len - 8
    if declared != fixed_size:
        struct.pack_into("<I", blob, 4, fixed_size)

    info = {
        "declared_riff_size": declared,
        "patched_riff_size": fixed_size,
        "fmt_tag": None,
        "has_x2st": False,
        "data_size": None,
    }

    # Walk chunks for diagnostics.
    off = 12
    while off + 8 <= file_len:
        tag = bytes(blob[off:off + 4])
        sz = struct.unpack_from("<I", blob, off + 4)[0]
        if tag == b"fmt ":
            info["fmt_tag"] = struct.unpack_from("<H", blob, off + 8)[0]
        elif tag == b"x2st":
            info["has_x2st"] = True
        elif tag == b"data":
            info["data_size"] = sz
            break
        off += 8 + sz
        if sz == 0:
            break

    return blob, info


def run_vgmstream(vgmstream: str, xma_path: Path, wav_path: Path) -> tuple[bool, str]:
    try:
        proc = subprocess.run(
            [vgmstream, "-o", str(wav_path), str(xma_path)],
            capture_output=True,
            text=True,
            timeout=120,
        )
    except FileNotFoundError:
        return False, f"vgmstream not found: {vgmstream}"
    except subprocess.TimeoutExpired:
        return False, "vgmstream timed out"
    if proc.returncode != 0:
        return False, (proc.stderr or proc.stdout or "vgmstream failed").strip().splitlines()[-1]
    return True, ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", type=Path, default=DEFAULT_INPUT,
                    help=f"FHM-typed root (default: {DEFAULT_INPUT})")
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                    help=f"Output directory (default: {DEFAULT_OUTPUT})")
    ap.add_argument("--packs", action="store_true",
                    help="Also split bgmpack/voicepack/demopack from --assets.")
    ap.add_argument("--packs-only", action="store_true",
                    help="Skip the FHM corpus and only split the asset packs.")
    ap.add_argument("--assets", type=Path, default=DEFAULT_ASSETS,
                    help=f"Asset directory containing *.bin packs (default: {DEFAULT_ASSETS})")
    ap.add_argument("--vgmstream", default=shutil.which("vgmstream-cli"),
                    help="Path to vgmstream-cli. If found on PATH it's used by default.")
    ap.add_argument("--no-decode", action="store_true",
                    help="Just copy/patch .xma files; skip vgmstream decode.")
    ap.add_argument("--dry-run", action="store_true",
                    help="List what would be exported and exit.")
    args = ap.parse_args()

    do_fhm = not args.packs_only
    do_packs = args.packs or args.packs_only

    if do_fhm and not args.input.is_dir():
        print(f"input not found: {args.input}", file=sys.stderr)
        return 2

    riffs = find_riffs(args.input) if do_fhm else []
    if do_fhm:
        print(f"found {len(riffs)} RIFF entries under {args.input}")

    pack_paths: list[Path] = []
    if do_packs:
        if not args.assets.is_dir():
            print(f"assets dir not found: {args.assets}", file=sys.stderr)
            return 2
        pack_paths = [args.assets / n for n in PACK_FILES if (args.assets / n).is_file()]
        print(f"found {len(pack_paths)} asset packs under {args.assets}")

    if args.dry_run:
        for r in riffs:
            print("  ", derive_output_name(r, args.input), f"({r.stat().st_size} B)")
        for p in pack_paths:
            print("  pack:", p.name, f"({p.stat().st_size} B)")
        return 0

    args.output.mkdir(parents=True, exist_ok=True)

    decode = not args.no_decode and args.vgmstream is not None
    if not args.no_decode and args.vgmstream is None:
        print("note: vgmstream-cli not on PATH; writing .xma only "
              "(re-run with --vgmstream <path> to decode to .wav)")

    decoded = 0
    failed = 0

    pack_written = 0
    pack_xma_files: list[Path] = []
    if do_packs:
        pack_dir = args.output / "packs"
        pack_dir.mkdir(parents=True, exist_ok=True)
        for p in pack_paths:
            print(f"splitting {p.name}...")
            pack_written += split_pack(p, pack_dir)
        pack_xma_files = sorted(pack_dir.glob("*.xma"))

    if decode and pack_xma_files:
        print(f"decoding {len(pack_xma_files)} pack streams via vgmstream...")
        for xma_path in pack_xma_files:
            wav_path = xma_path.with_suffix(".wav")
            if wav_path.is_file() and wav_path.stat().st_size > 0:
                decoded += 1
                continue
            ok, err = run_vgmstream(args.vgmstream, xma_path, wav_path)
            if ok:
                decoded += 1
            else:
                failed += 1
                print(f"    {xma_path.name}: {err}")

    for riff in riffs:
        out_name = derive_output_name(riff, args.input)
        xma_path = args.output / out_name
        blob = bytearray(riff.read_bytes())
        try:
            patched, info = patch_riff(blob)
        except ValueError as exc:
            print(f"  skip {riff}: {exc}")
            failed += 1
            continue
        xma_path.write_bytes(patched)

        tag = info["fmt_tag"]
        tag_str = f"0x{tag:04X}" if tag is not None else "?"
        marker = "x2st" if info["has_x2st"] else "----"
        print(f"  {out_name}  fmt={tag_str} {marker} data={info['data_size']}")

        if decode:
            wav_path = xma_path.with_suffix(".wav")
            ok, err = run_vgmstream(args.vgmstream, xma_path, wav_path)
            if ok:
                decoded += 1
            else:
                failed += 1
                print(f"    decode failed: {err}")

    print(f"done. fhm_riffs={len(riffs)} pack_streams={pack_written} "
          f"wav_decoded={decoded} failures={failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
