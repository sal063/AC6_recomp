#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageFilter


DDS_MAGIC = 0x20534444
DDS_PF_ALPHAPIXELS = 0x00000001
DDS_PF_RGB = 0x00000040
DDS_HEADER_FLAGS_TEXTURE = 0x00001007
DDS_HEADER_FLAGS_PITCH = 0x00000008
DDS_HEADER_FLAGS_MIPMAP = 0x00020000
DDS_CAPS_TEXTURE = 0x00001000
DDS_CAPS_COMPLEX = 0x00000008
DDS_CAPS_MIPMAP = 0x00400000


def be16(blob: bytes, offset: int) -> int:
    return struct.unpack_from(">H", blob, offset)[0]


def be32(blob: bytes, offset: int) -> int:
    return struct.unpack_from(">I", blob, offset)[0]


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def safe_ascii(blob: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in blob)


@dataclass
class ExportPlan:
    layout: str
    format_name: str
    visible_width: int
    visible_height: int
    storage_width: int
    storage_height: int
    mip_count: int
    payload_offset: int
    payload_size: int
    mip_sizes: list[int]
    notes: list[str]


def bc_mip0_storage_size(width: int, height: int, bytes_per_block: int) -> int:
    width_blocks = (width + 3) // 4
    height_blocks = (height + 3) // 4
    return align_up(width_blocks, 32) * align_up(height_blocks, 32) * bytes_per_block


def looks_like_gray_argb(payload: bytes) -> bool:
    if len(payload) < 4 or len(payload) % 4 != 0:
        return False
    for i in range(0, len(payload), 4):
        if payload[i] != 0xFF:
            return False
        if payload[i + 1] != payload[i + 2] or payload[i + 2] != payload[i + 3]:
            return False
    return True


def classify_ntxr(blob: bytes) -> tuple[ExportPlan | None, str | None]:
    if len(blob) < 0x60 or blob[:4] != b"NTXR":
        return None, "not_ntxr"

    variant_a = be16(blob, 0x20)
    variant_b = be16(blob, 0x22)
    width = be16(blob, 0x24)
    height = be16(blob, 0x26)
    declared_payload_size = be32(blob, 0x18)

    if width == 0 or height == 0 or declared_payload_size == 0:
        return None, "invalid_dimensions"

    explicit_mip_sizes = []
    if variant_a > 1 and len(blob) >= 0x40 + (variant_a * 4):
        explicit_mip_sizes = [be32(blob, 0x40 + (i * 4)) for i in range(variant_a)]

    if (
        variant_a > 1
        and explicit_mip_sizes
        and all(size > 0 for size in explicit_mip_sizes)
        and sum(explicit_mip_sizes) == declared_payload_size
        and len(blob) >= 0x70 + declared_payload_size
        and variant_b == 2
    ):
        first_mip_size = explicit_mip_sizes[0]
        if first_mip_size == width * height * 4:
            format_name = "rgba8"
        elif first_mip_size == width * height:
            format_name = "r8"
        else:
            return None, f"unsupported_explicit_mip_size_{first_mip_size}"
        return (
            ExportPlan(
                layout=f"{format_name}_mipped_0x70",
                format_name=format_name,
                visible_width=width,
                visible_height=height,
                storage_width=width,
                storage_height=height,
                mip_count=variant_a,
                payload_offset=0x70,
                payload_size=declared_payload_size,
                mip_sizes=explicit_mip_sizes,
                notes=["explicit mip sizes at 0x40"],
            ),
            None,
        )

    if (
        variant_a > 1
        and variant_b in (1, 2)
        and len(explicit_mip_sizes) >= 2
        and len(blob) >= 0x1000 + declared_payload_size
    ):
        bc3_mip0_size = bc_mip0_storage_size(width, height, 16)
        if explicit_mip_sizes[0] == bc3_mip0_size and explicit_mip_sizes[0] + explicit_mip_sizes[1] == declared_payload_size:
            return (
                ExportPlan(
                    layout="bc3_swap16_mipped_0x1000",
                    format_name="bc3_swap16",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=variant_a,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=explicit_mip_sizes,
                    notes=["packed BC3 mip chain", "16-bit endian-swapped blocks", "preview exports mip 0"],
                ),
                None,
            )

    if variant_a == 1 and variant_b == 19 and declared_payload_size == width * height * 4:
        # Some variant_b=19 textures store a short header only, while others
        # have a larger metadata/padding block and the texel payload starts at
        # 0x1000. Prefer the larger offset when present - it fixes the torn
        # aircraft/body atlases and still produces sane results for the simpler
        # UI textures.
        if len(blob) >= 0x1000 + declared_payload_size:
            payload_offset = 0x1000
            layout = "rgba8_single_0x1000"
            notes = ["variant_b=19", "ARGB payload at 0x1000"]
        elif len(blob) >= 0x60 + declared_payload_size:
            payload_offset = 0x60
            layout = "rgba8_single_0x60"
            notes = ["variant_b=19", "ARGB payload at 0x60"]
        else:
            return None, "truncated_rgba_single"
        return (
            ExportPlan(
                layout=layout,
                format_name="rgba8",
                visible_width=width,
                visible_height=height,
                storage_width=width,
                storage_height=height,
                mip_count=1,
                payload_offset=payload_offset,
                payload_size=declared_payload_size,
                mip_sizes=[declared_payload_size],
                notes=notes,
            ),
            None,
        )

    if variant_b == 0 and len(blob) >= 0x1000 + declared_payload_size:
        bc1_mip0_size = bc_mip0_storage_size(width, height, 8)
        bc3_mip0_size = bc_mip0_storage_size(width, height, 16)
        if variant_a == 1 and declared_payload_size == bc1_mip0_size * 6:
            return (
                ExportPlan(
                    layout="bc1_swap16_cube6_0x1000",
                    format_name="bc1_swap16_cube6",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=1,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=[bc1_mip0_size] * 6,
                    notes=["six BC1 faces", "16-bit endian-swapped blocks", "preview exports contact sheet"],
                ),
                None,
            )
        if variant_a == 1 and declared_payload_size == bc1_mip0_size:
            return (
                ExportPlan(
                    layout="bc1_swap16_single_0x1000",
                    format_name="bc1_swap16",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=1,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=[declared_payload_size],
                    notes=["BC1 texture", "16-bit endian-swapped blocks"],
                ),
                None,
            )
        if variant_a == 1 and declared_payload_size == bc3_mip0_size:
            return (
                ExportPlan(
                    layout="bc3_swap16_single_0x1000",
                    format_name="bc3_swap16",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=1,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=[declared_payload_size],
                    notes=["BC3 texture", "16-bit endian-swapped blocks"],
                ),
                None,
            )
        if (
            variant_a > 1
            and len(explicit_mip_sizes) >= 2
            and explicit_mip_sizes[0] == bc1_mip0_size
            and explicit_mip_sizes[0] + explicit_mip_sizes[1] == declared_payload_size
        ):
            return (
                ExportPlan(
                    layout="bc1_swap16_mipped_0x1000",
                    format_name="bc1_swap16",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=variant_a,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=explicit_mip_sizes,
                    notes=["packed BC1 mip chain", "16-bit endian-swapped blocks", "preview exports mip 0"],
                ),
                None,
            )

    if variant_b == 0:
        bc1_mip0_size = bc_mip0_storage_size(width, height, 8)
        bc3_mip0_size = bc_mip0_storage_size(width, height, 16)
        payload_offset = None
        if len(blob) >= 0x1000 + declared_payload_size:
            payload_offset = 0x1000
        elif len(blob) >= 0x70 + declared_payload_size:
            payload_offset = 0x70
        elif len(blob) >= 0x60 + declared_payload_size:
            payload_offset = 0x60
        if payload_offset is not None:
            if variant_a == 1 and declared_payload_size == bc1_mip0_size * 6:
                return (
                    ExportPlan(
                        layout=f"bc1_swap16_cube6_{payload_offset:#x}",
                        format_name="bc1_swap16_cube6",
                        visible_width=width,
                        visible_height=height,
                        storage_width=width,
                        storage_height=height,
                        mip_count=1,
                        payload_offset=payload_offset,
                        payload_size=declared_payload_size,
                        mip_sizes=[bc1_mip0_size] * 6,
                        notes=["six BC1 faces", "16-bit endian-swapped blocks", "preview exports contact sheet"],
                    ),
                    None,
                )
            if variant_a == 1 and declared_payload_size == bc1_mip0_size:
                return (
                    ExportPlan(
                        layout=f"bc1_swap16_single_{payload_offset:#x}",
                        format_name="bc1_swap16",
                        visible_width=width,
                        visible_height=height,
                        storage_width=width,
                        storage_height=height,
                        mip_count=1,
                        payload_offset=payload_offset,
                        payload_size=declared_payload_size,
                        mip_sizes=[declared_payload_size],
                        notes=["BC1 texture", "16-bit endian-swapped blocks"],
                    ),
                    None,
                )
            if variant_a == 1 and declared_payload_size == bc3_mip0_size:
                return (
                    ExportPlan(
                        layout=f"bc3_swap16_single_{payload_offset:#x}",
                        format_name="bc3_swap16",
                        visible_width=width,
                        visible_height=height,
                        storage_width=width,
                        storage_height=height,
                        mip_count=1,
                        payload_offset=payload_offset,
                        payload_size=declared_payload_size,
                        mip_sizes=[declared_payload_size],
                        notes=["BC3 texture", "16-bit endian-swapped blocks"],
                    ),
                    None,
                )
            if (
                variant_a > 1
                and len(explicit_mip_sizes) >= 2
                and explicit_mip_sizes[0] == bc1_mip0_size
                and explicit_mip_sizes[0] + explicit_mip_sizes[1] == declared_payload_size
            ):
                return (
                    ExportPlan(
                        layout=f"bc1_swap16_mipped_{payload_offset:#x}",
                        format_name="bc1_swap16",
                        visible_width=width,
                        visible_height=height,
                        storage_width=width,
                        storage_height=height,
                        mip_count=variant_a,
                        payload_offset=payload_offset,
                        payload_size=declared_payload_size,
                        mip_sizes=explicit_mip_sizes,
                        notes=["packed BC1 mip chain", "16-bit endian-swapped blocks", "preview exports mip 0"],
                    ),
                    None,
                )

    if variant_a == 1 and variant_b == 1 and len(blob) >= 0x1000 + declared_payload_size:
        bc3_mip0_size = bc_mip0_storage_size(width, height, 16)
        if declared_payload_size == bc3_mip0_size:
            return (
                ExportPlan(
                    layout="bc3_swap16_single_0x1000_b1",
                    format_name="bc3_swap16",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=1,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=[declared_payload_size],
                    notes=["variant_b=1", "BC3 texture", "16-bit endian-swapped blocks"],
                ),
                None,
            )

    if variant_a == 1 and variant_b == 20 and declared_payload_size == width * height * 4:
        payload_offset = 0x60
        if len(blob) >= payload_offset + declared_payload_size:
            payload = blob[payload_offset : payload_offset + declared_payload_size]
            if looks_like_gray_argb(payload):
                return (
                    ExportPlan(
                        layout="gray_argb_linear_0x60",
                        format_name="gray_argb_linear",
                        visible_width=width,
                        visible_height=height,
                        storage_width=width,
                        storage_height=height,
                        mip_count=1,
                        payload_offset=payload_offset,
                        payload_size=declared_payload_size,
                        mip_sizes=[declared_payload_size],
                        notes=["variant_b=20", "linear grayscale stored as opaque ARGB"],
                    ),
                    None,
                )

    if variant_a == 1 and variant_b == 2:
        storage_width = align_up(width, 128)
        storage_height = align_up(height, 128)
        if declared_payload_size == storage_width * storage_height and len(blob) >= 0x1000 + declared_payload_size:
            return (
                ExportPlan(
                    layout="r8_single_aligned_0x1000",
                    format_name="r8",
                    visible_width=width,
                    visible_height=height,
                    storage_width=storage_width,
                    storage_height=storage_height,
                    mip_count=1,
                    payload_offset=0x1000,
                    payload_size=declared_payload_size,
                    mip_sizes=[declared_payload_size],
                    notes=["128-aligned backing rectangle"],
                ),
                None,
            )

    if variant_a == 1 and variant_b == 1 and declared_payload_size == width * height:
        if len(blob) >= 0x9000 + declared_payload_size:
            return (
                ExportPlan(
                    layout="r8_single_0x9000",
                    format_name="r8",
                    visible_width=width,
                    visible_height=height,
                    storage_width=width,
                    storage_height=height,
                    mip_count=1,
                    payload_offset=0x9000,
                    payload_size=declared_payload_size,
                    mip_sizes=[declared_payload_size],
                    notes=["0x9000 payload offset"],
                ),
                None,
            )

    return None, f"unsupported_variant_a{variant_a}_b{variant_b}"


def build_legacy_bgra_dds(width: int, height: int, mip_payloads_bgra: list[bytes]) -> bytes:
    header_flags = DDS_HEADER_FLAGS_TEXTURE | DDS_HEADER_FLAGS_PITCH
    caps = DDS_CAPS_TEXTURE
    if len(mip_payloads_bgra) > 1:
        header_flags |= DDS_HEADER_FLAGS_MIPMAP
        caps |= DDS_CAPS_COMPLEX | DDS_CAPS_MIPMAP

    pitch = width * 4
    header_values = [
        124,
        header_flags,
        height,
        width,
        pitch,
        0,
        max(len(mip_payloads_bgra), 1),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        32,
        DDS_PF_RGB | DDS_PF_ALPHAPIXELS,
        0,
        32,
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000,
        caps,
        0,
        0,
        0,
        0,
    ]
    assert len(header_values) == 31
    header = struct.pack("<31I", *header_values)
    return struct.pack("<I", DDS_MAGIC) + header + b"".join(mip_payloads_bgra)


def rgba_to_bgra(rgba: bytes) -> bytes:
    bgra = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        r = rgba[i + 0]
        g = rgba[i + 1]
        b = rgba[i + 2]
        a = rgba[i + 3]
        bgra[i + 0] = b
        bgra[i + 1] = g
        bgra[i + 2] = r
        bgra[i + 3] = a
    return bytes(bgra)


def argb_to_rgba(argb: bytes) -> bytes:
    rgba = bytearray(len(argb))
    for i in range(0, len(argb), 4):
        a = argb[i + 0]
        r = argb[i + 1]
        g = argb[i + 2]
        b = argb[i + 3]
        rgba[i + 0] = r
        rgba[i + 1] = g
        rgba[i + 2] = b
        rgba[i + 3] = a
    return bytes(rgba)


def gray_to_rgba(gray: bytes) -> bytes:
    rgba = bytearray(len(gray) * 4)
    out = 0
    for value in gray:
        rgba[out + 0] = value
        rgba[out + 1] = value
        rgba[out + 2] = value
        rgba[out + 3] = 255
        out += 4
    return bytes(rgba)


def swap16(data: bytes) -> bytes:
    out = bytearray(len(data))
    for i in range(0, len(data), 2):
        out[i : i + 2] = data[i : i + 2][::-1]
    return bytes(out)


def rgb565(color: int) -> tuple[int, int, int]:
    return (
        ((color >> 11) & 31) * 255 // 31,
        ((color >> 5) & 63) * 255 // 63,
        (color & 31) * 255 // 31,
    )


def decode_bc4_block(block: bytes) -> list[int]:
    alpha0 = block[0]
    alpha1 = block[1]
    bits = int.from_bytes(block[2:8], "little")
    values = [alpha0, alpha1]
    if alpha0 > alpha1:
        values += [
            (6 * alpha0 + 1 * alpha1) // 7,
            (5 * alpha0 + 2 * alpha1) // 7,
            (4 * alpha0 + 3 * alpha1) // 7,
            (3 * alpha0 + 4 * alpha1) // 7,
            (2 * alpha0 + 5 * alpha1) // 7,
            (1 * alpha0 + 6 * alpha1) // 7,
        ]
    else:
        values += [
            (4 * alpha0 + 1 * alpha1) // 5,
            (3 * alpha0 + 2 * alpha1) // 5,
            (2 * alpha0 + 3 * alpha1) // 5,
            (1 * alpha0 + 4 * alpha1) // 5,
            0,
            255,
        ]
    return [values[(bits >> (3 * i)) & 7] for i in range(16)]


def decode_bc3_to_rgba(data: bytes, width: int, height: int) -> bytes:
    blocks_w = (width + 3) // 4
    blocks_h = (height + 3) // 4
    out = bytearray(width * height * 4)
    cursor = 0
    for block_y in range(blocks_h):
        for block_x in range(blocks_w):
            alpha = decode_bc4_block(data[cursor : cursor + 8])
            color0 = int.from_bytes(data[cursor + 8 : cursor + 10], "little")
            color1 = int.from_bytes(data[cursor + 10 : cursor + 12], "little")
            color_bits = int.from_bytes(data[cursor + 12 : cursor + 16], "little")
            cursor += 16

            r0, g0, b0 = rgb565(color0)
            r1, g1, b1 = rgb565(color1)
            colors = [(r0, g0, b0), (r1, g1, b1)]
            if color0 > color1:
                colors += [
                    ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3),
                    ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3),
                ]
            else:
                colors += [
                    ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2),
                    (0, 0, 0),
                ]

            for py in range(4):
                for px in range(4):
                    x = block_x * 4 + px
                    y = block_y * 4 + py
                    if x >= width or y >= height:
                        continue
                    color_index = (color_bits >> (2 * (py * 4 + px))) & 3
                    r, g, b = colors[color_index]
                    a = alpha[py * 4 + px]
                    out_index = (y * width + x) * 4
                    out[out_index : out_index + 4] = bytes((r, g, b, a))
    return bytes(out)


def decode_bc1_to_rgba(data: bytes, width: int, height: int) -> bytes:
    blocks_w = (width + 3) // 4
    blocks_h = (height + 3) // 4
    out = bytearray(width * height * 4)
    cursor = 0
    for block_y in range(blocks_h):
        for block_x in range(blocks_w):
            color0 = int.from_bytes(data[cursor : cursor + 2], "little")
            color1 = int.from_bytes(data[cursor + 2 : cursor + 4], "little")
            color_bits = int.from_bytes(data[cursor + 4 : cursor + 8], "little")
            cursor += 8

            r0, g0, b0 = rgb565(color0)
            r1, g1, b1 = rgb565(color1)
            colors = [(r0, g0, b0, 255), (r1, g1, b1, 255)]
            if color0 > color1:
                colors += [
                    ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
                    ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255),
                ]
            else:
                colors += [
                    ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255),
                    (0, 0, 0, 0),
                ]

            for py in range(4):
                for px in range(4):
                    x = block_x * 4 + px
                    y = block_y * 4 + py
                    if x >= width or y >= height:
                        continue
                    color = colors[(color_bits >> (2 * (py * 4 + px))) & 3]
                    out_index = (y * width + x) * 4
                    out[out_index : out_index + 4] = bytes(color)
    return bytes(out)


def tiled_row(y: int, width: int, log2_bpp: int) -> int:
    macro = ((y // 32) * (width // 32)) << (log2_bpp + 7)
    micro = ((y & 6) << 2) << log2_bpp
    return macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 8) << (3 + log2_bpp)) + (
        (y & 1) << 4
    )


def tiled_col(x: int, y: int, log2_bpp: int, base_offset: int) -> int:
    macro = (x // 32) << (log2_bpp + 7)
    micro = (x & 7) << log2_bpp
    offset = base_offset + (macro + ((micro & ~0xF) << 1) + (micro & 0xF))
    return (
        ((offset & ~0x1FF) << 3)
        + ((offset & 0x1C0) << 2)
        + (offset & 0x3F)
        + ((y & 16) << 7)
        + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)
    )


def untile_blocks(src: bytes, width: int, height: int, pitch: int, bytes_per_block: int) -> bytes:
    log2_bpp = (bytes_per_block // 4) + ((bytes_per_block // 2) >> (bytes_per_block // 4))
    out = bytearray(width * height * bytes_per_block)
    for y in range(height):
        base = tiled_row(y, pitch, log2_bpp)
        row_off = y * width * bytes_per_block
        for x in range(width):
            off = tiled_col(x, y, log2_bpp, base) >> log2_bpp
            src_off = off * bytes_per_block
            dst_off = row_off + x * bytes_per_block
            out[dst_off:dst_off + bytes_per_block] = src[src_off:src_off + bytes_per_block]
    return bytes(out)


def write_tga_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        width,
        height,
        32,
        0x28,
    )
    path.write_bytes(header + rgba_to_bgra(rgba))


def write_tga_gray(path: Path, width: int, height: int, gray: bytes) -> None:
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,
        0,
        3,
        0,
        0,
        0,
        0,
        0,
        width,
        height,
        8,
        0x20,
    )
    path.write_bytes(header + gray)


def write_png_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    Image.frombytes("RGBA", (width, height), rgba).save(path)


def write_png_gray(path: Path, width: int, height: int, gray: bytes) -> None:
    Image.frombytes("L", (width, height), gray).save(path)


def write_png_r8_alpha(path: Path, width: int, height: int, gray: bytes) -> None:
    alpha = Image.frombytes("L", (width, height), gray)
    rgba = Image.new("RGBA", (width, height), (255, 255, 255, 0))
    rgba.putalpha(alpha)
    rgba.save(path)


def write_png_r8_preview(path: Path, width: int, height: int, gray: bytes) -> None:
    # These one-channel atlases are often glyph / mask data. A lightly smoothed
    # composited preview is easier to inspect than raw dithered luma.
    alpha = Image.frombytes("L", (width, height), gray).filter(ImageFilter.BoxBlur(0.5))
    preview = Image.new("RGBA", (width, height), (20, 20, 24, 255))
    fg = Image.new("RGBA", (width, height), (245, 245, 245, 255))
    fg.putalpha(alpha)
    preview.alpha_composite(fg)
    preview.save(path)


def extract_r8_visible(payload: bytes, storage_width: int, visible_width: int, visible_height: int) -> bytes:
    rows = []
    for row in range(visible_height):
        start = row * storage_width
        rows.append(payload[start:start + visible_width])
    return b"".join(rows)


def export_ntxr(input_path: Path, output_root: Path, source_root: Path) -> dict:
    blob = input_path.read_bytes()
    plan, reason = classify_ntxr(blob)
    output_base = output_root / input_path.relative_to(source_root)
    entry = {
        "source": str(input_path.relative_to(source_root)).replace("\\", "/"),
        "size": len(blob),
        "header": {
            "variant_a": be16(blob, 0x20) if len(blob) >= 0x22 else None,
            "variant_b": be16(blob, 0x22) if len(blob) >= 0x24 else None,
            "width": be16(blob, 0x24) if len(blob) >= 0x26 else None,
            "height": be16(blob, 0x26) if len(blob) >= 0x28 else None,
            "declared_payload_size": be32(blob, 0x18) if len(blob) >= 0x1C else None,
            "tag_0x40": safe_ascii(blob[0x40:0x44]) if len(blob) >= 0x44 else None,
            "tag_0x50": safe_ascii(blob[0x50:0x54]) if len(blob) >= 0x54 else None,
        },
    }
    if plan is None:
        stale_paths = (
            output_base.with_suffix(".dds"),
            output_base.with_suffix(".tga"),
            output_base.with_suffix(".png"),
            output_base.with_suffix(".json"),
            output_base.with_name(output_base.stem + ".raw.png"),
            output_base.with_name(output_base.stem + ".alpha.png"),
        )
        for stale_path in stale_paths:
            if stale_path.exists():
                stale_path.unlink()
        for stale_face in output_base.parent.glob(output_base.stem + ".face*.png"):
            stale_face.unlink()
        entry["status"] = "skipped"
        entry["reason"] = reason
        return entry

    payload = blob[plan.payload_offset:plan.payload_offset + plan.payload_size]
    output_base.parent.mkdir(parents=True, exist_ok=True)

    preview_path = output_base.with_suffix(".tga")
    png_path = output_base.with_suffix(".png")
    dds_path = output_base.with_suffix(".dds")
    json_path = output_base.with_suffix(".json")
    raw_png_path = output_base.with_name(output_base.stem + ".raw.png")
    alpha_png_path = output_base.with_name(output_base.stem + ".alpha.png")

    dds_rgba_payloads: list[bytes] = []
    preview_rgba: bytes | None = None
    preview_gray: bytes | None = None
    preview_width = plan.visible_width
    preview_height = plan.visible_height
    face_pngs: list[str] = []

    if plan.format_name == "rgba8":
        cursor = 0
        for mip_index, mip_size in enumerate(plan.mip_sizes):
            mip_blob = payload[cursor:cursor + mip_size]
            if len(mip_blob) != mip_size:
                entry["status"] = "skipped"
                entry["reason"] = "truncated_mip_payload"
                return entry
            untiled_mip_blob = untile_blocks(
                mip_blob,
                max(plan.visible_width >> mip_index, 1),
                max(plan.visible_height >> mip_index, 1),
                max(plan.storage_width >> mip_index, 1),
                4,
            )
            rgba_mip = argb_to_rgba(untiled_mip_blob)
            dds_rgba_payloads.append(rgba_mip)
            if mip_index == 0:
                preview_rgba = rgba_mip[: plan.visible_width * plan.visible_height * 4]
            cursor += mip_size
        assert preview_rgba is not None
        write_tga_rgba(preview_path, plan.visible_width, plan.visible_height, preview_rgba)
        write_png_rgba(png_path, plan.visible_width, plan.visible_height, preview_rgba)
    elif plan.format_name == "r8":
        cursor = 0
        mip_width = plan.visible_width
        mip_height = plan.visible_height
        for mip_index, mip_size in enumerate(plan.mip_sizes):
            mip_blob = payload[cursor:cursor + mip_size]
            if len(mip_blob) != mip_size:
                entry["status"] = "skipped"
                entry["reason"] = "truncated_mip_payload"
                return entry
            storage_width = max(plan.storage_width >> mip_index, 1)
            untiled_gray = untile_blocks(mip_blob, mip_width, mip_height, storage_width, 1)
            expected_size = mip_width * mip_height
            visible_gray = untiled_gray[:expected_size]
            dds_rgba_payloads.append(gray_to_rgba(visible_gray))
            if mip_index == 0:
                preview_gray = visible_gray
            cursor += mip_size
            mip_width = max(mip_width >> 1, 1)
            mip_height = max(mip_height >> 1, 1)
        assert preview_gray is not None
        write_tga_gray(preview_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_r8_preview(png_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_gray(raw_png_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_r8_alpha(alpha_png_path, plan.visible_width, plan.visible_height, preview_gray)
    elif plan.format_name == "bc3_swap16":
        width_blocks = (plan.visible_width + 3) // 4
        height_blocks = (plan.visible_height + 3) // 4
        pitch_blocks = align_up(width_blocks, 32)
        mip0_size = plan.mip_sizes[0]
        mip0_blob = payload[:mip0_size]
        untiled = untile_blocks(mip0_blob, width_blocks, height_blocks, pitch_blocks, 16)
        preview_rgba = decode_bc3_to_rgba(swap16(untiled), plan.visible_width, plan.visible_height)
        dds_rgba_payloads.append(preview_rgba)
        write_tga_rgba(preview_path, plan.visible_width, plan.visible_height, preview_rgba)
        write_png_rgba(png_path, plan.visible_width, plan.visible_height, preview_rgba)
    elif plan.format_name == "bc1_swap16":
        width_blocks = (plan.visible_width + 3) // 4
        height_blocks = (plan.visible_height + 3) // 4
        pitch_blocks = align_up(width_blocks, 32)
        mip0_size = plan.mip_sizes[0]
        mip0_blob = payload[:mip0_size]
        untiled = untile_blocks(mip0_blob, width_blocks, height_blocks, pitch_blocks, 8)
        preview_rgba = decode_bc1_to_rgba(swap16(untiled), plan.visible_width, plan.visible_height)
        dds_rgba_payloads.append(preview_rgba)
        write_tga_rgba(preview_path, plan.visible_width, plan.visible_height, preview_rgba)
        write_png_rgba(png_path, plan.visible_width, plan.visible_height, preview_rgba)
    elif plan.format_name == "bc1_swap16_cube6":
        width_blocks = (plan.visible_width + 3) // 4
        height_blocks = (plan.visible_height + 3) // 4
        pitch_blocks = align_up(width_blocks, 32)
        face_size = plan.mip_sizes[0]
        faces: list[bytes] = []
        for face_index in range(6):
            face_blob = payload[face_index * face_size : (face_index + 1) * face_size]
            untiled = untile_blocks(face_blob, width_blocks, height_blocks, pitch_blocks, 8)
            face_rgba = decode_bc1_to_rgba(swap16(untiled), plan.visible_width, plan.visible_height)
            faces.append(face_rgba)
            face_path = output_base.with_name(output_base.stem + f".face{face_index}.png")
            write_png_rgba(face_path, plan.visible_width, plan.visible_height, face_rgba)
            face_pngs.append(str(face_path.relative_to(output_root)).replace("\\", "/"))
        dds_rgba_payloads.append(faces[0])
        preview_width = plan.visible_width * 3
        preview_height = plan.visible_height * 2
        preview_image = Image.new("RGBA", (preview_width, preview_height), (0, 0, 0, 255))
        for face_index, face_rgba in enumerate(faces):
            x = (face_index % 3) * plan.visible_width
            y = (face_index // 3) * plan.visible_height
            preview_image.paste(Image.frombytes("RGBA", (plan.visible_width, plan.visible_height), face_rgba), (x, y))
        preview_rgba = preview_image.tobytes()
        write_tga_rgba(preview_path, preview_width, preview_height, preview_rgba)
        write_png_rgba(png_path, preview_width, preview_height, preview_rgba)
    elif plan.format_name == "gray_argb_linear":
        gray = payload[1::4]
        preview_gray = gray
        dds_rgba_payloads.append(gray_to_rgba(gray))
        write_tga_gray(preview_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_r8_preview(png_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_gray(raw_png_path, plan.visible_width, plan.visible_height, preview_gray)
        write_png_r8_alpha(alpha_png_path, plan.visible_width, plan.visible_height, preview_gray)
    else:
        entry["status"] = "skipped"
        entry["reason"] = f"unhandled_format_{plan.format_name}"
        return entry

    dds_bytes = build_legacy_bgra_dds(
        width=plan.visible_width,
        height=plan.visible_height,
        mip_payloads_bgra=[rgba_to_bgra(mip) for mip in dds_rgba_payloads],
    )
    dds_path.write_bytes(dds_bytes)
    json_path.write_text(
        json.dumps(
            {
                "source": entry["source"],
                "layout": plan.layout,
                "format": plan.format_name,
                "visible_width": plan.visible_width,
                "visible_height": plan.visible_height,
                "storage_width": plan.storage_width,
                "storage_height": plan.storage_height,
                "mip_count": plan.mip_count,
                "payload_offset": plan.payload_offset,
                "payload_size": plan.payload_size,
                "dds_encoding": "legacy_a8r8g8b8_view",
                "notes": plan.notes,
                "preview_png": png_path.name,
                "raw_png": raw_png_path.name if plan.format_name in ("r8", "gray_argb_linear") else None,
                "alpha_png": alpha_png_path.name if plan.format_name in ("r8", "gray_argb_linear") else None,
                "preview_width": preview_width,
                "preview_height": preview_height,
                "face_pngs": face_pngs if face_pngs else None,
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    entry["status"] = "exported"
    entry["layout"] = plan.layout
    entry["format"] = plan.format_name
    entry["dds"] = str(dds_path.relative_to(output_root)).replace("\\", "/")
    entry["preview"] = str(preview_path.relative_to(output_root)).replace("\\", "/")
    entry["preview_png"] = str(png_path.relative_to(output_root)).replace("\\", "/")
    if plan.format_name in ("r8", "gray_argb_linear"):
        entry["raw_preview_png"] = str(raw_png_path.relative_to(output_root)).replace("\\", "/")
        entry["alpha_preview_png"] = str(alpha_png_path.relative_to(output_root)).replace("\\", "/")
    if face_pngs:
        entry["face_pngs"] = face_pngs
    entry["metadata"] = str(json_path.relative_to(output_root)).replace("\\", "/")
    entry["visible_width"] = plan.visible_width
    entry["visible_height"] = plan.visible_height
    entry["storage_width"] = plan.storage_width
    entry["storage_height"] = plan.storage_height
    entry["mip_count"] = plan.mip_count
    entry["notes"] = plan.notes
    return entry


def main() -> int:
    parser = argparse.ArgumentParser(description="Export known AC6 NTXR textures to DDS and TGA.")
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("out") / "ac6_runtime_fhm_typed",
        help="Root directory containing extracted .ntxr files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("out") / "ac6_runtime_ntxr_exported",
        help="Output directory for exported textures",
    )
    args = parser.parse_args()

    source_root = args.input.resolve()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    exported = []
    skipped = []
    for input_path in sorted(source_root.rglob("*.ntxr")):
        result = export_ntxr(input_path, output_root, source_root)
        if result["status"] == "exported":
            exported.append(result)
        else:
            skipped.append(result)

    manifest = {
        "input": str(source_root),
        "output": str(output_root),
        "exported_count": len(exported),
        "skipped_count": len(skipped),
        "exported": exported,
        "skipped": skipped,
    }
    (output_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        json.dumps(
            {
                "exported_count": len(exported),
                "skipped_count": len(skipped),
                "output": str(output_root),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
