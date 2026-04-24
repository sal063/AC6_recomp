#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


def be32(blob: bytes, offset: int) -> int:
    return struct.unpack_from(">I", blob, offset)[0]


def read_c_string(blob: bytes, offset: int) -> str:
    end = blob.find(b"\0", offset)
    if end < 0:
        end = len(blob)
    return blob[offset:end].decode("ascii", errors="replace")


def extract_ascii_strings(blob: bytes) -> list[dict[str, int | str]]:
    results = []
    for match in re.finditer(rb"[ -~]{4,}", blob):
        text = match.group().decode("ascii", errors="replace")
        if text.count("?") > len(text) // 2:
            continue
        results.append({"offset": match.start(), "text": text})
    return results


def detect_texture_table(blob: bytes, ntxr_count: int) -> tuple[int, list[dict[str, int]]] | tuple[None, list]:
    if ntxr_count <= 0:
        return None, []

    max_start = max(0, len(blob) - ntxr_count * 12)
    for start in range(0, max_start + 1, 4):
        entries = []
        ok = True
        for index in range(ntxr_count):
            off = start + index * 12
            tex_id = be32(blob, off + 0)
            width, height = struct.unpack_from(">HH", blob, off + 4)
            flags = be32(blob, off + 8)
            if tex_id != index:
                ok = False
                break
            if width <= 0 or height <= 0 or width > 8192 or height > 8192:
                ok = False
                break
            entries.append(
                {
                    "texture_id": tex_id,
                    "width": width,
                    "height": height,
                    "flags": flags,
                }
            )
        if ok:
            return start, entries
    return None, []


def parse_one(swg_path: Path) -> dict:
    blob = swg_path.read_bytes()
    if blob[:4] != b"SWG\0":
        raise ValueError(f"{swg_path} is not an SWG blob")

    sibling_ntxrs = sorted(swg_path.parent.glob("*_NTXR.ntxr"))
    table_offset, texture_table = detect_texture_table(blob, len(sibling_ntxrs))
    if texture_table:
        for entry in texture_table:
            candidate = swg_path.parent / f"{entry['texture_id'] + 1:03d}_NTXR.ntxr"
            entry["candidate_ntxr"] = candidate.name if candidate.exists() else None

    strings = extract_ascii_strings(blob)
    return {
        "source": str(swg_path),
        "magic": blob[:4].decode("ascii", errors="replace"),
        "widget_name": read_c_string(blob, 0x08) if len(blob) > 0x08 else "",
        "size": len(blob),
        "sibling_ntxr_count": len(sibling_ntxrs),
        "texture_table_offset": table_offset,
        "texture_table": texture_table,
        "strings": strings,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse AC6 SWG UI metadata blobs.")
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("out") / "ac6_runtime_fhm_typed",
        help="SWG file or directory to scan",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("out") / "ac6_runtime_swg_parsed",
        help="Output directory for parsed SWG json files",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    swg_files = [input_path] if input_path.is_file() else sorted(input_path.rglob("*_SWG_.bin"))
    parsed = []
    for swg_path in swg_files:
        result = parse_one(swg_path)
        relative = swg_path.relative_to(input_path.parent if input_path.is_file() else input_path)
        out_path = output_root / relative.with_suffix(".json")
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
        parsed.append(
            {
                "source": str(relative).replace("\\", "/"),
                "output": str(out_path.relative_to(output_root)).replace("\\", "/"),
                "widget_name": result["widget_name"],
                "texture_table_offset": result["texture_table_offset"],
                "texture_count": len(result["texture_table"]),
            }
        )

    manifest = {
        "input": str(input_path),
        "output": str(output_root),
        "parsed_count": len(parsed),
        "files": parsed,
    }
    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"parsed_count": len(parsed), "output": str(output_root)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
