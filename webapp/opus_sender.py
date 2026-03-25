"""Opus placeholder.

The initial repo implementation streams PCM first so the end-to-end path is usable with minimal setup.
This file exists as the seam where a libopus/opuslib frame encoder can be dropped in later.
"""


class OpusNotConfiguredError(RuntimeError):
    pass


def encode_pcm_frame_to_opus(_pcm: bytes, _sample_rate: int, _bitrate: int) -> bytes:
    raise OpusNotConfiguredError(
        "Opus streaming is not wired in yet. Use codec=pcm for the current minimal working build."
    )
