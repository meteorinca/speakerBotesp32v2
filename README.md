# speakerBotesp32v2

Minimal working starter for an ESP32-S3 speaker bot plus a Flask control/streaming webapp.

## What is included

- `esp32s3_audio_bot/`: ESP-IDF firmware scaffold
  - Wi-Fi station mode skeleton
  - HTTP control API skeleton (`/status`, `/servo`, `/oled`, `/stream/start`, `/stream/stop`)
  - UDP audio receive task with simple packet header parsing
  - ring-buffered PCM playback path placeholder
  - I2S / OLED / servo modules split into separate files for extension
- `webapp/`: Flask app
  - local web UI
  - upload MP3/WAV/etc
  - uses `ffmpeg` to decode to 48 kHz mono PCM
  - sends control requests to ESP
  - streams audio over UDP with simple pacing
  - servo and OLED controls

## Current streaming mode

The Python app ships with a **working PCM-over-UDP path first** because it is the fastest way to get a usable end-to-end prototype. The packet format and code structure are set up so you can swap the sender/receiver to Opus framing later without changing the control plane.

## Webapp quick start

Requirements:

- Python 3.10+
- `ffmpeg` installed and available on PATH

Setup:

```bash
cd webapp
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
```

Open <http://127.0.0.1:5000>

## ESP-IDF quick start

```bash
cd esp32s3_audio_bot
idf.py set-target esp32s3
idf.py build
```

You will still need to tune pins, board power, I2S wiring, SSD1306 configuration, and optionally swap in `esp_audio_codec` if you want full Opus on-device.

## Packet format

Each UDP packet uses a compact header:

```text
[magic 2 bytes]
[version 1 byte]
[type 1 byte]
[seq 4 bytes]
[timestamp_ms 4 bytes]
[payload_len 2 bytes]
[payload ...]
```

- magic: `0xB0 0x7E`
- version: `1`
- type: `0x01` for PCM audio frames

## API

- `GET /status`
- `POST /servo` → `{ "angle": 90 }`
- `POST /oled` → `{ "line1": "Hello", "line2": "World" }`
- `POST /stream/start` → `{ "udp_port": 5006, "codec": "pcm", "sample_rate": 48000, "frame_ms": 20 }`
- `POST /stream/stop`

## Notes

This repo is intentionally minimal but structured like a real project so you can iterate cleanly.
