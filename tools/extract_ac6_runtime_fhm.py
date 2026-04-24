#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import struct
from collections import defaultdict
from pathlib import Path


DUMP_RE = re.compile(
    r"^entry_(?P<record_id>\d+)_mode(?P<mode>\d+)_c(?P<compressed_size>\d+)_u(?P<decompressed_size>\d+)"
    r"(?:_off(?P<source_offset>[0-9a-fA-F]+))?\.bin$"
)


def load_manifest_entries(path: Path) -> dict[tuple[int, int], list[dict]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    by_pair: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for entry in manifest["entries"]:
        if entry["storage_kind"] != "compressed":
            continue
        by_pair[(entry["compressed_size"], entry["decompressed_size"])].append(entry)
    return by_pair


def parse_fhm(blob: bytes) -> list[dict]:
    if len(blob) < 0x1C or blob[:4] != b"FHM ":
        return []

    count = struct.unpack_from(">I", blob, 0x10)[0]
    if count == 0:
        return []

    table_base = 0x14
    offsets_base = table_base
    sizes_base = offsets_base + (count * 4)
    if sizes_base + (count * 4) > len(blob):
        return []

    offsets = [struct.unpack_from(">I", blob, offsets_base + (i * 4))[0] for i in range(count)]
    sizes = [struct.unpack_from(">I", blob, sizes_base + (i * 4))[0] for i in range(count)]

    entries = []
    for index, (offset, size) in enumerate(zip(offsets, sizes)):
        if offset >= len(blob):
            continue

        end = offset + size
        if end > len(blob):
            next_offset = offsets[index + 1] if index + 1 < len(offsets) else len(blob)
            end = min(next_offset, len(blob))
        if end <= offset:
            continue

        child = blob[offset:end]
        entries.append(
            {
                "index": index,
                "offset": offset,
                "size": len(child),
                "magic": child[:4].decode("ascii", errors="replace"),
                "data": child,
            }
        )
    return entries


def safe_name(name: str) -> str:
    return "".join(ch if ch.isalnum() or ch in ("-", "_", ".") else "_" for ch in name)


def magic_extension(magic: str) -> str:
    normalized = magic.strip().upper()
    mapping = {
        "FHM": ".fhm",
        "NTXR": ".ntxr",
        "NSXR": ".nsxr",
        "MDLP": ".mdlp",
        "PLAD": ".plad",
        "BFX": ".bfx",
        "BSN": ".bsn",
        "ACE6": ".ace6",
        "NFH": ".nfh",
    }
    return mapping.get(normalized, ".bin")


def extract_container(blob: bytes, container_dir: Path, output_root: Path, depth: int,
                      max_depth: int) -> list[dict]:
    children = parse_fhm(blob)
    if not children:
        return []

    child_entries = []
    for child in children:
        safe_magic = safe_name(child["magic"])
        child_name = f"{child['index']:03d}_{safe_magic}{magic_extension(child['magic'])}"
        child_path = container_dir / child_name
        child_path.write_bytes(child["data"])

        child_entry = {
            "index": child["index"],
            "offset": child["offset"],
            "size": child["size"],
            "magic": child["magic"],
            "path": str(child_path.relative_to(output_root)).replace("\\", "/"),
        }

        if depth < max_depth and child["data"][:4] == b"FHM ":
            nested_dir = container_dir / f"{child['index']:03d}_{safe_magic}"
            nested_dir.mkdir(parents=True, exist_ok=True)
            nested_children = extract_container(child["data"], nested_dir, output_root, depth + 1,
                                                max_depth)
            if nested_children:
                child_entry["nested"] = nested_children

        child_entries.append(child_entry)

    return child_entries


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract child payloads from runtime-dumped AC6 FHM containers.")
    parser.add_argument(
        "--dump-dir",
        type=Path,
        default=Path("out") / "ac6_pac_runtime_dump",
        help="Directory containing runtime PAC decode dumps",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("out") / "ac6_pac_extracted_raw" / "manifest.json",
        help="Manifest produced by extract_ac6_pac.py",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("out") / "ac6_runtime_fhm_extracted",
        help="Output directory for parsed FHM containers and child payloads",
    )
    parser.add_argument(
        "--max-depth",
        type=int,
        default=4,
        help="Maximum nested FHM recursion depth",
    )
    args = parser.parse_args()

    dump_dir = args.dump_dir.resolve()
    manifest_path = args.manifest.resolve()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    by_pair = load_manifest_entries(manifest_path)
    extracted = []
    selected_dumps: dict[tuple[int, int, int, int], Path] = {}

    for dump_path in sorted(dump_dir.glob("*.bin")):
        match = DUMP_RE.match(dump_path.name)
        if not match:
            continue

        meta = match.groupdict()
        key = (
            int(meta["record_id"]),
            int(meta["mode"]),
            int(meta["compressed_size"]),
            int(meta["decompressed_size"]),
        )
        current = selected_dumps.get(key)
        if current is None:
            selected_dumps[key] = dump_path
            continue

        current_match = DUMP_RE.match(current.name)
        assert current_match is not None
        current_has_offset = current_match.groupdict()["source_offset"] is not None
        new_has_offset = meta["source_offset"] is not None
        if new_has_offset and not current_has_offset:
            selected_dumps[key] = dump_path

    for dump_path in sorted(selected_dumps.values()):
        match = DUMP_RE.match(dump_path.name)
        assert match is not None

        meta = match.groupdict()
        compressed_size = int(meta["compressed_size"])
        decompressed_size = int(meta["decompressed_size"])
        codec_mode = int(meta["mode"])
        record_id = int(meta["record_id"])
        source_offset = int(meta["source_offset"], 16) if meta["source_offset"] else None
        candidates = by_pair.get((compressed_size, decompressed_size), [])

        base_label = (
            f"idx_{candidates[0]['index']:04d}"
            if len(candidates) == 1
            else f"pair_c{compressed_size}_u{decompressed_size}"
        )
        container_dir = output_root / safe_name(base_label)
        container_dir.mkdir(parents=True, exist_ok=True)

        blob = dump_path.read_bytes()
        children = parse_fhm(blob)
        if not children:
            raw_path = container_dir / dump_path.name
            raw_path.write_bytes(blob)
            extracted.append(
                {
                    "dump": dump_path.name,
                    "record_id": record_id,
                    "codec_mode": codec_mode,
                    "compressed_size": compressed_size,
                    "decompressed_size": decompressed_size,
                    "source_offset": source_offset,
                    "candidate_indexes": [entry["index"] for entry in candidates],
                    "kind": "raw",
                    "path": str(raw_path.relative_to(output_root)).replace("\\", "/"),
                }
            )
            continue

        child_entries = extract_container(blob, container_dir, output_root, 0, args.max_depth)

        extracted.append(
            {
                "dump": dump_path.name,
                "record_id": record_id,
                "codec_mode": codec_mode,
                "compressed_size": compressed_size,
                "decompressed_size": decompressed_size,
                "source_offset": source_offset,
                "candidate_indexes": [entry["index"] for entry in candidates],
                "kind": "fhm",
                "child_count": len(child_entries),
                "children": child_entries,
            }
        )

    manifest = {
        "dump_dir": str(dump_dir),
        "manifest": str(manifest_path),
        "output": str(output_root),
        "containers": extracted,
    }
    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        json.dumps(
            {
                "containers": len(extracted),
                "output": str(output_root),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
