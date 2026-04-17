#!/usr/bin/env python3
"""
Rewrite legacy AC6 / Rexglue graphics config keys to the planned native renderer
namespace and annotate keys that must be removed manually.

This is intentionally conservative:
- it only rewrites simple `key = value` style lines
- it preserves unrelated lines verbatim
- it emits a report listing renamed and deprecated keys
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


KEY_RE = re.compile(r"^(?P<indent>\s*)(?P<key>[A-Za-z0-9_\.]+)(?P<sep>\s*=\s*)(?P<value>.*?)(?P<comment>\s*(?:#.*)?)$")


@dataclass(frozen=True)
class Mapping:
    replacement: str | None
    note: str


KEY_MAPPINGS: dict[str, Mapping] = {
    "ac6_native_graphics_bootstrap": Mapping("ac6_renderer.bootstrap", "Renamed."),
    "ac6_native_present_enabled": Mapping("ac6_renderer.enabled", "Renamed."),
    "ac6_native_present_enable_postfx": Mapping(
        "ac6_renderer.postfx.enabled", "Renamed."
    ),
    "ac6_native_present_enable_ui_compose": Mapping(
        "ac6_renderer.ui.compose", "Renamed."
    ),
    "ac6_native_present_force_pm4_fallback": Mapping(
        "ac6_renderer.compat.force_legacy_present",
        "Legacy fallback remains temporary; remove after native cutover.",
    ),
    "ac6_native_present_allow_unstable": Mapping(
        "ac6_renderer.compat.allow_experimental", "Renamed."
    ),
    "trace_gpu_prefix": Mapping(
        "ac6_renderer.debug.trace_prefix",
        "Renamed. Native trace format differs from PM4 traces.",
    ),
    "trace_gpu_stream": Mapping(
        "ac6_renderer.debug.trace_stream",
        "Renamed. Native trace format differs from PM4 traces.",
    ),
    "swap_post_effect": Mapping(
        "ac6_renderer.postfx.anti_aliasing",
        "Map values manually if they depended on emulation-only swap effects.",
    ),
    "vsync": Mapping("ac6_renderer.present.vsync", "Renamed."),
    "resolution_scale": Mapping("ac6_renderer.scaling.resolution_scale", "Renamed."),
    "draw_resolution_scale_x": Mapping("ac6_renderer.scaling.draw_scale_x", "Renamed."),
    "draw_resolution_scale_y": Mapping("ac6_renderer.scaling.draw_scale_y", "Renamed."),
    "async_shader_compilation": Mapping(
        "ac6_renderer.shader.async_pipeline_compile",
        "Renamed for native pipeline compilation.",
    ),
    "d3d12_bindless": Mapping(
        "ac6_renderer.backend.d3d12.bindless", "Renamed."
    ),
    "d3d12_submit_on_primary_buffer_end": Mapping(
        None,
        "Remove. Primary-buffer semantics are PM4-specific and have no native equivalent.",
    ),
    "d3d12_readback_memexport": Mapping(
        None,
        "Remove. Memexport readback is an emulation-only path.",
    ),
    "d3d12_readback_resolve": Mapping(
        None,
        "Remove. Resolve readback is an emulation-only path.",
    ),
    "vulkan_submit_on_primary_buffer_end": Mapping(
        None,
        "Remove. Primary-buffer semantics are PM4-specific and have no native equivalent.",
    ),
    "vulkan_readback_memexport": Mapping(
        None,
        "Remove. Memexport readback is an emulation-only path.",
    ),
    "vulkan_readback_resolve": Mapping(
        None,
        "Remove. Resolve readback is an emulation-only path.",
    ),
    "readback_resolve": Mapping(
        None,
        "Remove. Guest resolve readback does not exist in the native renderer.",
    ),
    "readback_memexport": Mapping(
        None,
        "Remove. Guest memexport readback does not exist in the native renderer.",
    ),
    "readback_memexport_fast": Mapping(
        None,
        "Remove. Guest memexport readback does not exist in the native renderer.",
    ),
    "native_2x_msaa": Mapping(
        "ac6_renderer.quality.msaa_2x",
        "Renamed. Validate against new AA / resolve policy.",
    ),
    "dump_shaders": Mapping(
        "ac6_renderer.debug.dump_shaders",
        "Renamed. Output now uses native compiled shader manifests.",
    ),
}


def transform_line(line: str) -> tuple[str, str | None]:
    match = KEY_RE.match(line)
    if not match:
        return line, None

    key = match.group("key")
    mapping = KEY_MAPPINGS.get(key)
    if mapping is None:
        return line, None

    indent = match.group("indent")
    sep = match.group("sep")
    value = match.group("value")
    comment = match.group("comment")

    if mapping.replacement is None:
        rewritten = (
            f"{indent}# DEPRECATED: {key}{sep}{value}{comment}\n"
            f"{indent}# NOTE: {mapping.note}\n"
        )
        return rewritten, f"deprecated {key}: {mapping.note}"

    rewritten = f"{indent}{mapping.replacement}{sep}{value}{comment}\n"
    if comment.strip():
        return rewritten, f"renamed {key} -> {mapping.replacement}"
    return (
        rewritten.rstrip("\n") + f"  # {mapping.note}\n",
        f"renamed {key} -> {mapping.replacement}",
    )


def migrate_text(text: str) -> tuple[str, list[str]]:
    out_lines: list[str] = []
    report: list[str] = []
    for original in text.splitlines(keepends=True):
        rewritten, event = transform_line(original)
        out_lines.append(rewritten)
        if event:
            report.append(event)
    return "".join(out_lines), report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Migrate legacy AC6 graphics config keys to the native renderer namespace."
    )
    parser.add_argument("input", type=pathlib.Path, help="Source config file")
    parser.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        help="Output config file. Defaults to <input>.native-migrated",
    )
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="Rewrite the input file in place.",
    )
    args = parser.parse_args()

    if args.in_place and args.output is not None:
        print("--in-place cannot be used together with --output", file=sys.stderr)
        return 2

    source = args.input.read_text(encoding="utf-8")
    migrated, report = migrate_text(source)

    if args.in_place:
        destination = args.input
    elif args.output is not None:
        destination = args.output
    else:
        destination = args.input.with_name(args.input.name + ".native-migrated")

    destination.write_text(migrated, encoding="utf-8")

    print(f"Wrote migrated config to: {destination}")
    if not report:
        print("No legacy graphics keys were rewritten.")
        return 0

    print("Migration report:")
    for entry in report:
        print(f"- {entry}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
