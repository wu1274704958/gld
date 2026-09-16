"""Build block-compressed (BC1/BC4) atlases and write them as DDS.

The AOE2DE SLD layers are already block compressed, so an atlas can be built by
copying 4x4 blocks instead of decoding each frame to pixels and encoding it
again. Re-encoding would quantise the image a second time; the copy is lossless
with respect to the source and much cheaper.

Only the main and shadow layers travel this path. The player-color layer is
rewritten by the exporter into palette-indexed weights that no longer match the
source blocks, and re-encoding it into BC4 was measured to leave 0.5% of pixels
off by up to 25/255 of the blend weight, so it stays a single-channel R8 image.
"""

from __future__ import annotations

import struct
from typing import Any

# BC1 stores two RGB565 endpoints plus 2-bit indices. Zero endpoints put the
# block in 3-colour mode, where index 3 is the transparent one, so setting every
# index to 3 gives a fully clear block. An all-zero BC1 block is NOT transparent
# - it decodes to opaque black - which is why this pattern exists.
TRANSPARENT_BC1 = bytes([0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF])

# BC4 has a single value and no alpha; index 0 selects the first endpoint, so
# an all-zero block already reads back as 0.
TRANSPARENT_BC4 = bytes(8)

BLOCK_BYTES = {"bc1": 8, "bc4": 8}
TRANSPARENT_BLOCK = {"bc1": TRANSPARENT_BC1, "bc4": TRANSPARENT_BC4}

# DXGI_FORMAT values, as used by the DDS DX10 extended header.
DXGI_BC1_UNORM = 71
DXGI_BC4_UNORM = 80
DXGI_FORMAT = {"bc1": DXGI_BC1_UNORM, "bc4": DXGI_BC4_UNORM}

DDS_MAGIC = 0x20534444
DDS_HEADER_SIZE = 124
DDS_PIXELFORMAT_SIZE = 32
DDS_RESOURCE_DIMENSION_TEXTURE2D = 2
DDS_FOURCC_DX10 = 0x30315844  # "DX10"


class BlockAtlasError(ValueError):
    pass


def pack_blocks(frames: list[Any], layout: Any, encoding: str) -> bytes:
    """Copy every frame's blocks into one atlas laid out by ``layout``.

    ``frames`` must carry ``raw_blocks`` (the frame's own block grid, row-major)
    and be a multiple of four in both dimensions, which the SLD layers already
    are. The atlas is returned as a flat block grid matching ``layout``.
    """
    if encoding not in BLOCK_BYTES:
        raise BlockAtlasError(f"unknown block encoding {encoding!r}")

    block_bytes = BLOCK_BYTES[encoding]
    transparent = TRANSPARENT_BLOCK[encoding]

    if layout.width % 4 or layout.height % 4:
        raise BlockAtlasError(
            f"atlas size {layout.width}x{layout.height} is not block aligned")

    atlas_blocks_x = layout.width // 4
    atlas = bytearray(transparent * (atlas_blocks_x * (layout.height // 4)))

    if len(layout.slots) != len(frames):
        raise BlockAtlasError("atlas layout does not match the frame count")

    for index, (frame, slot) in enumerate(zip(frames, layout.slots)):
        x, y, slot_w, slot_h = slot

        if frame.width % 4 or frame.height % 4:
            raise BlockAtlasError(
                f"frame {index} is {frame.width}x{frame.height}, not block aligned")
        if x % 4 or y % 4:
            raise BlockAtlasError(f"frame {index} lands at ({x}, {y}), not block aligned")
        if slot_w < frame.width or slot_h < frame.height:
            raise BlockAtlasError(f"frame {index} exceeds its atlas slot")

        raw = getattr(frame, "raw_blocks", None)
        if raw is None:
            # A frame with no source content (or no passthrough data) stays
            # transparent, which is what the byte pattern already provides.
            continue

        blocks_x = frame.width // 4
        blocks_y = frame.height // 4
        if len(raw) != blocks_x * blocks_y * block_bytes:
            raise BlockAtlasError(
                f"frame {index} has {len(raw)} block bytes, expected "
                f"{blocks_x * blocks_y * block_bytes}")

        dest_x = x // 4
        dest_y = y // 4
        row_bytes = blocks_x * block_bytes

        for row in range(blocks_y):
            src = row * row_bytes
            dest = ((dest_y + row) * atlas_blocks_x + dest_x) * block_bytes
            atlas[dest:dest + row_bytes] = raw[src:src + row_bytes]

    return bytes(atlas)


def build_dds(width: int, height: int, blocks: bytes, encoding: str) -> bytes:
    """Wrap a block grid in a DX10-header DDS container."""
    if encoding not in DXGI_FORMAT:
        raise BlockAtlasError(f"unknown block encoding {encoding!r}")
    if width % 4 or height % 4:
        raise BlockAtlasError(f"DDS size {width}x{height} is not block aligned")

    expected = (width // 4) * (height // 4) * BLOCK_BYTES[encoding]
    if len(blocks) != expected:
        raise BlockAtlasError(
            f"block data is {len(blocks)} bytes, expected {expected}")

    # 4 + 28 + 44 + 32 + 20 = 128 bytes: the magic, the seven DWORDs that lead
    # the header, dwReserved1, the pixel format, and the four caps plus the
    # trailing reserved DWORD.
    header = struct.pack(
        "<4s 7I 11I 8I 5I",
        b"DDS ",                     # dwMagic
        DDS_HEADER_SIZE,             # dwSize
        0x1 | 0x2 | 0x4 | 0x1000 | 0x80000,  # dwFlags: CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
        height,
        width,
        len(blocks),                 # dwPitchOrLinearSize
        0,                           # dwDepth
        1,                           # dwMipMapCount
        *([0] * 11),                 # dwReserved1
        DDS_PIXELFORMAT_SIZE,        # ddspf.dwSize
        0x4,                         # ddspf.dwFlags: DDPF_FOURCC
        DDS_FOURCC_DX10,             # ddspf.dwFourCC
        0,                           # ddspf.dwRGBBitCount
        0, 0, 0, 0,                  # ddspf masks
        0x1000,                      # dwCaps: DDSCAPS_TEXTURE
        0,                           # dwCaps2
        0,                           # dwCaps3
        0,                           # dwCaps4
        0,                           # dwReserved2
    )

    dx10 = struct.pack(
        "<5I",
        DXGI_FORMAT[encoding],
        DDS_RESOURCE_DIMENSION_TEXTURE2D,
        0,      # miscFlag
        1,      # arraySize
        0,      # miscFlags2
    )

    return header + dx10 + blocks


def write_dds(path, width: int, height: int, blocks: bytes, encoding: str) -> None:
    """Write ``blocks`` to ``path`` as a DX10 DDS."""
    path.write_bytes(build_dds(width, height, blocks, encoding))
