import subprocess
from pathlib import Path


def decode_to_pcm_stream(audio_path: Path, sample_rate: int = 48000, channels: int = 1):
    command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(audio_path),
        "-f",
        "s16le",
        "-acodec",
        "pcm_s16le",
        "-ac",
        str(channels),
        "-ar",
        str(sample_rate),
        "pipe:1",
    ]
    return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
