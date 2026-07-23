"""Small texture helper for local AoE2DE SLD export.

This mirrors the subset of openage's Texture/TextureImage behaviour needed by
the gld exporter.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy
from PIL import Image


@dataclass
class TextureImage:
    data: numpy.ndarray
    hotspot: tuple[int, int]
    source_ordinal: int
    source_frame_index: int

    @property
    def width(self) -> int:
        return int(self.data.shape[1])

    @property
    def height(self) -> int:
        return int(self.data.shape[0])

    def get_pil_image(self) -> Image.Image:
        return Image.fromarray(self.data, "RGBA")


class Texture:
    def __init__(self, sld: Any, layer: int = 0) -> None:
        self.frames: list[TextureImage] = []
        frames = sld.get_frames(layer)
        layer_bit = 1 << layer
        records = [record for record in sld.get_frame_records()
                   if int(record["frame_type"]) & layer_bit]
        if len(records) != len(frames):
            raise ValueError(
                f"SLD layer {layer} metadata count {len(records)} "
                f"differs from decoded frame count {len(frames)}"
            )

        for frame, record in zip(frames, records):
            data = frame.get_picture_data()
            if not isinstance(data, numpy.ndarray):
                data = numpy.array(data)
            self.frames.append(TextureImage(
                data.astype(numpy.uint8, copy=False),
                frame.get_hotspot(),
                int(record["ordinal"]),
                int(record["frame_index"]),
            ))
