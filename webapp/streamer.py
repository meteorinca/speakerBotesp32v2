import socket
import threading
import time
from pathlib import Path
from typing import Optional

import requests

from ffmpeg_decode import decode_to_pcm_stream
from opus_sender import OpusNotConfiguredError, encode_pcm_frame_to_opus
from packet import TYPE_PCM, build_packet


class SpeakerBotStreamer:
    def __init__(self):
        self.esp_ip: Optional[str] = None
        self.udp_port = 5006
        self.control_port = 80
        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._lock = threading.Lock()
        self._stats = {
            "connected": False,
            "streaming": False,
            "esp_ip": None,
            "codec": "pcm",
            "sample_rate": 48000,
            "frame_ms": 20,
            "sent_packets": 0,
            "last_error": None,
            "current_file": None,
        }

    def status(self):
        with self._lock:
            return dict(self._stats)

    def set_esp_ip(self, esp_ip: str):
        with self._lock:
            self.esp_ip = esp_ip
            self._stats["connected"] = True
            self._stats["esp_ip"] = esp_ip

    def _url(self, path: str) -> str:
        if not self.esp_ip:
            raise RuntimeError("ESP IP not configured")
        return f"http://{self.esp_ip}:{self.control_port}{path}"

    def fetch_esp_status(self):
        try:
            response = requests.get(self._url("/status"), timeout=2)
            response.raise_for_status()
            return response.json()
        except Exception as exc:
            with self._lock:
                self._stats["last_error"] = str(exc)
            return {"ok": False, "error": str(exc)}

    def set_servo(self, angle: int):
        angle = max(0, min(180, angle))
        try:
            response = requests.post(self._url("/servo"), json={"angle": angle}, timeout=2)
            response.raise_for_status()
            return {"ok": True, "angle": angle, "esp": response.json()}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def set_oled(self, line1: str, line2: str):
        try:
            response = requests.post(self._url("/oled"), json={"line1": line1, "line2": line2}, timeout=2)
            response.raise_for_status()
            return {"ok": True, "esp": response.json()}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def get_mic(self):
        try:
            response = requests.get(self._url("/mic"), timeout=2)
            response.raise_for_status()
            return {"ok": True, "esp": response.json()}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}


    def stop(self):
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        self._thread = None
        try:
            response = requests.post(self._url("/stream/stop"), json={}, timeout=2)
            payload = response.json() if response.ok else {"status": response.status_code}
        except Exception as exc:
            payload = {"ok": False, "error": str(exc)}
        with self._lock:
            self._stats["streaming"] = False
            self._stats["current_file"] = None
        return {"ok": True, "esp": payload}

    def play(self, audio_path: Path, frame_ms: int = 20, sample_rate: int = 48000, codec: str = "pcm", bitrate: int = 32000):
        if not self.esp_ip:
            return {"ok": False, "error": "ESP IP not configured"}
        if self._thread and self._thread.is_alive():
            return {"ok": False, "error": "stream already running"}

        self._stop_event.clear()
        start_payload = {
            "udp_port": self.udp_port,
            "codec": codec,
            "sample_rate": sample_rate,
            "frame_ms": frame_ms,
            "bitrate": bitrate,
        }
        try:
            response = requests.post(self._url("/stream/start"), json=start_payload, timeout=2)
            response.raise_for_status()
        except Exception as exc:
            return {"ok": False, "error": f"failed to start ESP stream: {exc}"}

        with self._lock:
            self._stats.update({
                "streaming": True,
                "codec": codec,
                "sample_rate": sample_rate,
                "frame_ms": frame_ms,
                "sent_packets": 0,
                "last_error": None,
                "current_file": audio_path.name,
            })

        self._thread = threading.Thread(
            target=self._stream_worker,
            kwargs={
                "audio_path": audio_path,
                "frame_ms": frame_ms,
                "sample_rate": sample_rate,
                "codec": codec,
                "bitrate": bitrate,
            },
            daemon=True,
        )
        self._thread.start()
        return {"ok": True, "message": f"streaming {audio_path.name}"}

    def _stream_worker(self, audio_path: Path, frame_ms: int, sample_rate: int, codec: str, bitrate: int):
        channels = 1
        bytes_per_sample = 2
        samples_per_frame = int(sample_rate * (frame_ms / 1000.0))
        pcm_bytes_per_frame = samples_per_frame * channels * bytes_per_sample
        seq = 0
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        process = None
        next_deadline = time.perf_counter()

        try:
            process = decode_to_pcm_stream(audio_path, sample_rate=sample_rate, channels=channels)
            assert process.stdout is not None
            while not self._stop_event.is_set():
                frame = process.stdout.read(pcm_bytes_per_frame)
                if not frame:
                    break
                if len(frame) < pcm_bytes_per_frame:
                    frame = frame + (bytes(pcm_bytes_per_frame - len(frame)))
                if codec == "opus":
                    payload = encode_pcm_frame_to_opus(frame, sample_rate=sample_rate, bitrate=bitrate)
                    packet_type = 2
                else:
                    payload = frame
                    packet_type = TYPE_PCM
                packet = build_packet(packet_type=packet_type, seq=seq, payload=payload)
                sock.sendto(packet, (self.esp_ip, self.udp_port))
                seq += 1
                with self._lock:
                    self._stats["sent_packets"] = seq
                next_deadline += frame_ms / 1000.0
                sleep_for = next_deadline - time.perf_counter()
                if sleep_for > 0:
                    time.sleep(sleep_for)
        except OpusNotConfiguredError as exc:
            with self._lock:
                self._stats["last_error"] = str(exc)
        except Exception as exc:
            with self._lock:
                self._stats["last_error"] = str(exc)
        finally:
            self._stop_event.set()
            sock.close()
            if process is not None:
                process.kill()
            with self._lock:
                self._stats["streaming"] = False
                self._stats["current_file"] = None
