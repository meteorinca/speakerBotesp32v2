import struct
import time

MAGIC = bytes([0xB0, 0x7E])
VERSION = 1
TYPE_PCM = 1

HEADER_STRUCT = struct.Struct("!2sBBIIH")


def build_packet(packet_type: int, seq: int, payload: bytes) -> bytes:
    timestamp_ms = int(time.time() * 1000) & 0xFFFFFFFF
    header = HEADER_STRUCT.pack(MAGIC, VERSION, packet_type, seq & 0xFFFFFFFF, timestamp_ms, len(payload))
    return header + payload
