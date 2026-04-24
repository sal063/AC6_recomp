#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from pathlib import Path


HEADER_SIZE = 8
ENTRY_SIZE = 16


def parse_tbl(path: Path) -> list[dict]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError("DATA.TBL is too small")

    entry_count, pack_count = struct.unpack_from(">II", data, 0)
    expected_size = HEADER_SIZE + (entry_count * ENTRY_SIZE)
    if len(data) != expected_size:
        raise ValueError(f"unexpected DATA.TBL size: got {len(data)}, expected {expected_size}")

    entries = []
    for index in range(entry_count):
        group, offset, compressed_size, decompressed_size = struct.unpack_from(
            ">4I", data, HEADER_SIZE + (index * ENTRY_SIZE)
        )
        pac_name = "DATA01.PAC" if (group & 0x01000000) else "DATA00.PAC"
        storage_kind = "raw" if (group & 0x00020000) else "compressed"
        entries.append(
            {
                "index": index,
                "group": group,
                "group_hex": f"0x{group:08x}",
                "pac_name": pac_name,
                "storage_kind": storage_kind,
                "offset": offset,
                "compressed_size": compressed_size,
                "decompressed_size": decompressed_size,
            }
        )
    return entries


def sha256_path(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
      while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def extract_entries(asset_root: Path, output_root: Path, entries: list[dict], include_compressed: bool) -> dict:
    pac_bytes = {
        "DATA00.PAC": asset_root.joinpath("DATA00.PAC").read_bytes(),
        "DATA01.PAC": asset_root.joinpath("DATA01.PAC").read_bytes(),
    }
    pac_sizes = {name: len(data) for name, data in pac_bytes.items()}

    output_root.mkdir(parents=True, exist_ok=True)
    files_dir = output_root / "files"
    files_dir.mkdir(exist_ok=True)

    manifest_entries = []
    extracted_count = 0
    skipped_count = 0

    for entry in entries:
        if entry["storage_kind"] == "compressed" and not include_compressed:
            skipped_count += 1
            manifest_entries.append({**entry, "extracted": False, "reason": "compressed entry skipped"})
            continue

        pac_name = entry["pac_name"]
        pac_size = pac_sizes[pac_name]
        start = entry["offset"]
        end = start + entry["compressed_size"]
        if end > pac_size:
            raise ValueError(
                f"entry {entry['index']} exceeds {pac_name}: offset=0x{start:x}, size=0x{entry['compressed_size']:x}"
            )

        blob = pac_bytes[pac_name][start:end]
        subdir = files_dir / pac_name.replace(".PAC", "") / entry["storage_kind"]
        subdir.mkdir(parents=True, exist_ok=True)
        out_path = subdir / f"{entry['index']:04d}.bin"
        out_path.write_bytes(blob)

        manifest_entries.append(
            {
                **entry,
                "extracted": True,
                "path": str(out_path.relative_to(output_root)).replace("\\", "/"),
                "sha256": hashlib.sha256(blob).hexdigest(),
                "head_hex": blob[:32].hex(),
            }
        )
        extracted_count += 1

    manifest = {
        "asset_root": str(asset_root),
        "output_root": str(output_root),
        "entry_count": len(entries),
        "extracted_count": extracted_count,
        "skipped_count": skipped_count,
        "include_compressed": include_compressed,
        "archives": {
            name: {
                "size": size,
                "sha256": sha256_path(asset_root / name),
            }
            for name, size in pac_sizes.items()
        },
        "entries": manifest_entries,
    }

    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract indexed records from Ace Combat 6 DATA00/01.PAC using DATA.TBL.")
    parser.add_argument("asset_root", type=Path, help="Directory containing DATA.TBL, DATA00.PAC, and DATA01.PAC")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("out") / "ac6_pac_extracted_raw",
        help="Output directory for the manifest and extracted records",
    )
    parser.add_argument(
        "--raw-only",
        action="store_true",
        help="Extract only entries marked raw in DATA.TBL and skip compressed entries",
    )
    args = parser.parse_args()

    asset_root = args.asset_root.resolve()
    output_root = args.output.resolve()
    entries = parse_tbl(asset_root / "DATA.TBL")
    manifest = extract_entries(asset_root, output_root, entries, include_compressed=not args.raw_only)

    print(
        json.dumps(
            {
                "entry_count": manifest["entry_count"],
                "extracted_count": manifest["extracted_count"],
                "skipped_count": manifest["skipped_count"],
                "output_root": manifest["output_root"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
